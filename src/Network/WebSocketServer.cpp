#include "WebSocketServer.h"
#include "Preferences/Preferences.h"
#include "../Mappers/OutputMapper.h"
#include "../Mappers/InputMapper.h"

#if ENABLE_WEBSOCKETS

#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "HapticParser.h"
#include "imgui.h"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace {
    // Preference Keys
    const char* const kWebSocketSection = "WebSocket";
    const char* const kPortKey = "Port";
    const char* const kProtocolKey = "Protocol";
    const char* const kOutputDefIdKey = "OutputDefinitionId";
    const char* const kInputDefIdKey = "InputDefinitionId";
    const char* const kEnabledKey = "Enabled";
    const char* const kOutputEnabledKey = "OutputEnabled";
    const char* const kInputEnabledKey  = "InputEnabled";
    const char* const kInactivityTimeoutEnabledKey = "InactivityTimeoutEnabled";
    const char* const kInactivityTimeoutMsKey = "InactivityTimeoutMs";

    // Default values
    const char* const kDefaultProtocol = "Marsmaantje (New)";
}


struct us_listen_socket_t;

struct WebSocketServer::Impl {
    struct LogEntry { std::string text; bool isError = false; };

    int port = 4269;
    bool running = false;
    int runningPort = 0;
    bool restartPending = false;
    int restartPort = 0;
    std::mutex mutex;
    std::deque<LogEntry> logs;
    std::map<void *, std::string> clients;
    std::shared_ptr<IProtocol> protocol;
    std::string selectedProtocol;
    std::string selectedDefinitionId; // legacy single-slot
    std::string outputDefinitionId;   // user-selected output (server→client) definition
    std::string inputDefinitionId;    // user-selected input  (client→server) definition

    std::thread thread;
    uWS::App *app = nullptr;
    uWS::Loop *loop = nullptr;
    struct us_listen_socket_t *listen_socket = nullptr;
    // Preserved across Stop()/Start() cycles — mirrors OSCServer::m_savedOutputMapper.
    OutputMapper* savedOutputMapper = nullptr;

    // Inactivity timeout — updated on every received message.
    uint64_t lastMessageTime = 0;

    // Direction enable flags.
    bool outputEnabled = true;
    bool inputEnabled  = true;
    // Inactivity timeout — persisted to prefs.
    bool     inactivityTimeoutEnabled = true;
    uint64_t inactivityTimeoutMs      = 5000;
};

WebSocketServer &WebSocketServer::GetInstance() {
    static WebSocketServer instance;
    return instance;
}

WebSocketServer::WebSocketServer() : m_selectedDeviceId(0), m_Impl(new Impl) {
    // Default to Marsmaantje (New)
    SetProtocol(kDefaultProtocol);
}

WebSocketServer::~WebSocketServer() {
    Stop();
    if (m_Impl->thread.joinable()) {
        m_Impl->thread.join();
    }
    delete m_Impl;
}

void WebSocketServer::Start(int port) {
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        if (m_Impl->running)
            return;

        m_Impl->port = port;
        m_Impl->running = true;
        m_Impl->runningPort = port;
    }
    // Join outside the lock: the previous thread acquires m_Impl->mutex in its
    // exit block to set running=false.  Joining while holding the lock would
    // deadlock because the thread can never acquire the mutex to finish.
    if (m_Impl->thread.joinable()) {
        m_Impl->thread.join();
    }

    m_Impl->thread = std::thread([this, port]() {
        uWS::App app;
        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            m_Impl->app = &app;
            m_Impl->loop = uWS::Loop::get();
            m_Impl->clients.clear();

            // Restore the OutputMapper saved by Stop() so haptic effects keep
            // working across UI-driven Stop/Start cycles without requiring an
            // external SetOutputMapper() call.
            if (!m_OutputMapper && m_Impl->savedOutputMapper)
                m_OutputMapper = m_Impl->savedOutputMapper;
        }

        app.ws<int>("/*", {/* Settings */
                           .compression = uWS::SHARED_COMPRESSOR,
                           .maxPayloadLength = 16 * 1024 * 1024,
                           .idleTimeout = 16,

                           .open =
                               [this](auto *ws) {
                                   std::string ip(ws->getRemoteAddressAsText());
                                   std::lock_guard<std::mutex> lock(m_Impl->mutex);
                                   m_Impl->clients[ws] = ip;
                                   m_Impl->lastMessageTime = SDL_GetTicks();
                                   m_Impl->logs.push_back({"Client connected: " + ip, false});
                                   if (m_Impl->logs.size() > 100)
                                       m_Impl->logs.pop_front();
                               },
                           .message =
                               [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
                                   // Snapshot the state we need under the lock, then release it
                                   // before doing any heavy work (JSON parsing, OutputMapper calls).
                                   // Holding m_Impl->mutex during those calls:
                                   //   1. Stalls Broadcast / Stop / DrawContent on the main thread.
                                   //   2. Creates a lock-order hazard: WS mutex → OutputMapper mutex.
                                   //      If the main thread ever takes those in the opposite order,
                                   //      a deadlock results.
                                   std::shared_ptr<IProtocol> protoCopy;
                                   OutputMapper* mapperCopy = nullptr;
                                   bool inputEnabled = true;
                                   {
                                       std::lock_guard<std::mutex> lock(m_Impl->mutex);
                                       m_Impl->logs.push_back({"Client data: " + std::string(message), false});
                                       if (m_Impl->logs.size() > 100)
                                           m_Impl->logs.pop_front();
                                       m_Impl->lastMessageTime = SDL_GetTicks();
                                       protoCopy    = m_Impl->protocol;
                                       mapperCopy   = m_OutputMapper;
                                       inputEnabled = m_Impl->inputEnabled;
                                   }
                                   // Mutex is released — do slow work now.

                                   // Skip dispatch when input is disabled.
                                   if (!inputEnabled) return;

                                   bool handled = false;
                                   if (protoCopy) {
                                       handled = protoCopy->parse(std::string(message));
                                   }

                                   // Also parse for generic haptic commands
                                   HapticParser::Parse(message, mapperCopy);

                                   if (!handled) {
                                       std::lock_guard<std::mutex> errLock(m_Impl->mutex);
                                       std::string preview = std::string(message.substr(0, 80));
                                       if (message.size() > 80) preview += "...";
                                       m_Impl->logs.push_back({"Invalid WebSocket message: " + preview, true});
                                       if (m_Impl->logs.size() > 100) m_Impl->logs.pop_front();
                                   }
                               },
                           .close =
                               [this](auto *ws, int code, std::string_view message) {
                                   bool doStopHaptics = false;
                                   OutputMapper* mapperCopy = nullptr;
                                   {
                                       std::lock_guard<std::mutex> lock(m_Impl->mutex);
                                       if (m_Impl->clients.count(ws)) {
                                           m_Impl->logs.push_back({"Client disconnected: " + m_Impl->clients[ws], false});
                                           if (m_Impl->logs.size() > 100)
                                               m_Impl->logs.pop_front();
                                           m_Impl->clients.erase(ws);

                                           if (m_Impl->clients.empty()) {
                                               doStopHaptics = true;
                                               // m_OutputMapper is nulled by Stop() before deferred
                                               // close callbacks run — fall back to the saved copy so
                                               // haptics are always stopped on last-client-disconnect.
                                               mapperCopy = m_OutputMapper
                                                                ? m_OutputMapper
                                                                : m_Impl->savedOutputMapper;
                                           }
                                       }
                                   }

                                   if (doStopHaptics && mapperCopy) {
                                       mapperCopy->StopAllHapticEffects();
                                   }
                               }})
            .listen(port,
                    [this](auto *listen_socket) {
                        std::lock_guard<std::mutex> lock(m_Impl->mutex);
                        if (listen_socket) {
                            m_Impl->logs.push_back({"WebSocket server listening on port " + std::to_string(m_Impl->port), false});
                            m_Impl->listen_socket = (struct us_listen_socket_t *)listen_socket;
                        } else {
                            m_Impl->logs.push_back({"Failed to listen on port " + std::to_string(m_Impl->port), false});
                            m_Impl->running = false;
                        }
                        if (m_Impl->logs.size() > 100)
                            m_Impl->logs.pop_front();
                    })
            .run();

        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            m_Impl->app = nullptr;
            m_Impl->loop = nullptr;
            m_Impl->listen_socket = nullptr;
            m_Impl->running = false;
            m_Impl->runningPort = 0;
        }
    });
}

void WebSocketServer::Stop() {
    OutputMapper* mapper = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        m_Impl->restartPending = false;

        // Capture and immediately null m_OutputMapper while holding the lock.
        // The .close lambda (which fires on the uWS thread when clients are
        // disconnected) snapshots m_OutputMapper under this same mutex.  By
        // clearing it here — before the deferred client-close callbacks are
        // queued — we guarantee those callbacks see nullptr and will not call
        // StopAllHapticEffects() on an object that may have been freed by the
        // time the uWS thread processes the deferred work.
        // We also save a copy so that a subsequent Start() can restore it,
        // fixing the "effects stop after UI restart" bug.
        mapper = m_OutputMapper;
        m_Impl->savedOutputMapper = m_OutputMapper;
        m_OutputMapper = nullptr;

        // loop over all connected clients, closing them
        if (m_Impl->running && m_Impl->loop && !m_Impl->clients.empty()) {

            // We capture a copy of the clients map pointers to avoid long locks
            std::vector<void *> targets;
            for (auto const &[ws, ip] : m_Impl->clients) {
                targets.push_back(ws);
            }

            // Move the actual closing into the uWS Thread via defer
            m_Impl->loop->defer([targets, this]() {
                for (void *ptr : targets) {
                    // Check if the client is still connected to avoid use-after-free
                    {
                        std::lock_guard<std::mutex> lock(m_Impl->mutex);
                        if (m_Impl->clients.find(ptr) == m_Impl->clients.end())
                            continue;
                    }
                    // Cast back to the specific WebSocket type used in Start()
                    auto *ws = (uWS::WebSocket<false, true, int> *)ptr;
                    // code 1000 means "Normal closure" and gets send to all clients
                    ws->end(1000, "Server stopping");
                }
            });
        }

        if (m_Impl->running && m_Impl->loop && m_Impl->listen_socket) {
            struct us_listen_socket_t *socket = (struct us_listen_socket_t *)m_Impl->listen_socket;
            m_Impl->loop->defer([socket]() { us_listen_socket_close(0, socket); });
        }
    } // mutex released before external call

    // Stop haptic effects while the OutputMapper is still alive.  Called outside
    // the lock to avoid holding the WS mutex during an external call.
    if (mapper) {
        mapper->StopAllHapticEffects();
    }
}

void WebSocketServer::WaitStopped() {
    // Block until the uWS event-loop thread has fully exited.  After Stop()
    // defers the socket close, the thread continues running until app.run()
    // returns.  Joining here ensures no more .message/.close callbacks can
    // fire before the caller proceeds to tear down shared resources such as
    // OutputMapper.
    if (m_Impl->thread.joinable())
        m_Impl->thread.join();
}

bool WebSocketServer::IsRunning() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->running;
}

int WebSocketServer::GetPort() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->port;
}

void WebSocketServer::SetPort(int port) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->port = port;
}

int WebSocketServer::GetClientCount() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return (int)m_Impl->clients.size();
}

void WebSocketServer::SetSelectedDevice(int id) { m_selectedDeviceId = id; }

int WebSocketServer::GetSelectedDevice() const { return m_selectedDeviceId; }

void WebSocketServer::SetProtocol(const std::string& name) {
    auto proto = ProtocolManager::GetInstance().GetProtocol(name);
    if (proto) {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        m_Impl->protocol = proto;
        m_Impl->selectedProtocol = name;
    }
}

std::string WebSocketServer::GetProtocol() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->selectedProtocol;
}

void WebSocketServer::SetDefinition(const std::string& definitionId) {
    const ProtocolDefinition* def = nullptr;
    if (!definitionId.empty()) {
        def = ProtocolRegistry::GetInstance().FindById(definitionId);
    }

    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->selectedDefinitionId = definitionId;
    if (def) {
        // Sync port from the definition into the UI field
        m_Impl->port = def->wssPort;
    }
}

std::string WebSocketServer::GetDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->selectedDefinitionId;
}

void WebSocketServer::SetOutputDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->outputDefinitionId = definitionId;
    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) m_Impl->port = def->wssPort;
    }
}

void WebSocketServer::SetInputDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->inputDefinitionId = definitionId;
    // Input definitions don't change the listen port (server already listens on one port)
}

std::string WebSocketServer::GetOutputDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->outputDefinitionId;
}

std::string WebSocketServer::GetInputDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->inputDefinitionId;
}

void WebSocketServer::Broadcast(const std::string &msg, uWS::OpCode opCode) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);

    if (!m_Impl->outputEnabled) return;

    // Ensure the event loop is active
    if (m_Impl->loop && !m_Impl->clients.empty()) {

        // We capture a copy of the clients map pointers to avoid long locks
        std::vector<void *> targets;
        for (auto const &[ws, ip] : m_Impl->clients) {
            targets.push_back(ws);
        }

        // Move the actual sending into the uWS Thread via defer
        m_Impl->loop->defer([targets, msg, opCode, this]() {
            for (void *ptr : targets) {
                // Guard against use-after-free: a client that was in `targets`
                // when the snapshot was taken may have disconnected by the time
                // this deferred callback runs on the uWS thread.  The close
                // handler removes the entry from m_Impl->clients, so any pointer
                // absent here has already been freed by uWS.
                {
                    std::lock_guard<std::mutex> lock(m_Impl->mutex);
                    if (m_Impl->clients.find(ptr) == m_Impl->clients.end())
                        continue;
                }
                auto *ws = (uWS::WebSocket<false, true, int> *)ptr;
                ws->send(msg, opCode);
            }
        });
    }
}

void WebSocketServer::Broadcast(const std::string &address, float value) {
    // Avoid sending updates for unbound outputs
    if (!InputMapper::GetInstance().IsOutputAddressBound(address)) {
        return;
    }

    std::shared_ptr<IProtocol> protocol;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        protocol = m_Impl->protocol;
    }
    if (protocol) {
        std::string msg = protocol->format(address, value);
        uWS::OpCode opCode = (protocol->getProtocolName().find("OSC") != std::string::npos) ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
        Broadcast(msg, opCode);
    }
}

void WebSocketServer::Broadcast(const std::string &address, int value) {
    // Avoid sending updates for unbound outputs
    if (!InputMapper::GetInstance().IsOutputAddressBound(address)) {
        return;
    }

    std::shared_ptr<IProtocol> protocol;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        protocol = m_Impl->protocol;
    }
    if (protocol) {
        std::string msg = protocol->format(address, value);
        uWS::OpCode opCode = (protocol->getProtocolName().find("OSC") != std::string::npos) ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
        Broadcast(msg, opCode);
    }
}

void WebSocketServer::Broadcast(const std::string &address, const std::string &value) {
    // Avoid sending updates for unbound outputs
    if (!InputMapper::GetInstance().IsOutputAddressBound(address)) {
        return;
    }

    std::shared_ptr<IProtocol> protocol;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        protocol = m_Impl->protocol;
    }
    if (protocol) {
        std::string msg = protocol->format(address, value);
        uWS::OpCode opCode = (protocol->getProtocolName().find("OSC") != std::string::npos) ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
        Broadcast(msg, opCode);
    }
}

void WebSocketServer::Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll) {
    std::shared_ptr<IProtocol> protocol;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        protocol = m_Impl->protocol;
    }
    if (protocol) {
        InputMapper& im = InputMapper::GetInstance();
        std::map<std::string, float> values;
        if (im.IsOutputAddressBound("wheel"))    values["wheel"] = wheel;
        if (im.IsOutputAddressBound("brake"))    values["brake"] = brake;
        if (im.IsOutputAddressBound("throttle")) values["throttle"] = throttle;
        if (im.IsOutputAddressBound("pitch"))    values["pitch"] = pitch;
        if (im.IsOutputAddressBound("roll"))     values["roll"] = roll;

        if (values.empty()) {
            return;
        }

        std::string msg = protocol->format_wheel(values);
        if (!msg.empty()) {
            uWS::OpCode opCode = (protocol->getProtocolName().find("OSC") != std::string::npos) ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
            Broadcast(msg, opCode);
        }
    }
}

void WebSocketServer::LoadConfig(const PreferencesManager& prefs) {
    int port = prefs.GetInt(kWebSocketSection, kPortKey, 4269);
    std::string protocol = prefs.GetString(kWebSocketSection, kProtocolKey, kDefaultProtocol);
    std::string outDefId = prefs.GetString(kWebSocketSection, kOutputDefIdKey, "");
    std::string inDefId  = prefs.GetString(kWebSocketSection, kInputDefIdKey,  "");
    bool enabled = prefs.GetBool(kWebSocketSection, kEnabledKey, false);
    bool outputEnabled = prefs.GetBool(kWebSocketSection, kOutputEnabledKey, true);
    bool inputEnabled  = prefs.GetBool(kWebSocketSection, kInputEnabledKey,  true);
    bool timeoutEnabled = prefs.GetBool(kWebSocketSection, kInactivityTimeoutEnabledKey, true);
    int timeoutMs       = prefs.GetInt (kWebSocketSection, kInactivityTimeoutMsKey, 5000);

    SetPort(port);
    SetProtocol(protocol);

    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        m_Impl->outputEnabled = outputEnabled;
        m_Impl->inputEnabled  = inputEnabled;
        m_Impl->inactivityTimeoutEnabled = timeoutEnabled;
        m_Impl->inactivityTimeoutMs      = static_cast<uint64_t>(timeoutMs);
    }

    if (!outDefId.empty()) SetOutputDefinition(outDefId);
    if (!inDefId.empty())  SetInputDefinition(inDefId);

    if (enabled) Start(port);
}

void WebSocketServer::SaveConfig(PreferencesManager& prefs) {
    int port;
    std::string protocol, outDef, inDef;
    bool running, outEn, inEn, timeoutEn;
    uint64_t timeoutMs;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        port     = m_Impl->port;
        running  = m_Impl->running;
        protocol = m_Impl->selectedProtocol;
        outDef   = m_Impl->outputDefinitionId;
        inDef    = m_Impl->inputDefinitionId;
        outEn    = m_Impl->outputEnabled;
        inEn     = m_Impl->inputEnabled;
        timeoutEn = m_Impl->inactivityTimeoutEnabled;
        timeoutMs = m_Impl->inactivityTimeoutMs;
    }
    prefs.SetInt(kWebSocketSection,    kPortKey,               port);
    prefs.SetString(kWebSocketSection, kProtocolKey,           protocol);
    prefs.SetString(kWebSocketSection, kOutputDefIdKey, outDef);
    prefs.SetString(kWebSocketSection, kInputDefIdKey,  inDef);
    prefs.SetBool(kWebSocketSection,   kEnabledKey,            running);
    prefs.SetBool(kWebSocketSection,   kOutputEnabledKey,      outEn);
    prefs.SetBool(kWebSocketSection,   kInputEnabledKey,       inEn);
    prefs.SetBool(kWebSocketSection,   kInactivityTimeoutEnabledKey, timeoutEn);
    prefs.SetInt(kWebSocketSection,    kInactivityTimeoutMsKey,      static_cast<int>(timeoutMs));
}

void WebSocketServer::DrawContent() {
    // Handle pending restart
    {
        bool doRestart = false; int rPort = 0;
        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            if (m_Impl->restartPending && !m_Impl->running) {
                doRestart = true; rPort = m_Impl->restartPort;
                m_Impl->restartPending = false;
            }
        }
        if (doRestart) Start(rPort);
    }

    // Read all state under lock first
    bool running, restartPending;
    int  currentPort, runningPort, clientCount, timeoutMs;
    std::string outDefId, inDefId, currentProto;
    bool outputEnabled, inputEnabled;
    std::deque<Impl::LogEntry> logs;
    std::map<void*, std::string> clients;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        running        = m_Impl->running;
        currentPort    = m_Impl->port;
        runningPort    = m_Impl->runningPort;
        clientCount    = (int)m_Impl->clients.size();
        restartPending = m_Impl->restartPending;
        outDefId       = m_Impl->outputDefinitionId;
        inDefId        = m_Impl->inputDefinitionId;
        currentProto   = m_Impl->selectedProtocol;
        outputEnabled  = m_Impl->outputEnabled;
        inputEnabled   = m_Impl->inputEnabled;
        timeoutMs      = static_cast<int>(m_Impl->inactivityTimeoutMs);
        // m_Impl->inactivityTimeoutEnabled is read directly below
        logs           = m_Impl->logs;
        clients        = m_Impl->clients;
    }

    int portInput = currentPort;
    if (ImGui::InputInt("Port", &portInput)) SetPort(portInput);

    // ── Shared combo helper ───────────────────────────────────────────────────
    // We draw two independent combos: Output Protocol and Input Protocol.
    // Each shows built-in protocols (no direction) + filtered user definitions.
    struct Entry {
        std::string label;
        bool isSeparator  = false;
        bool isDefinition = false;
        std::string protocolName; // built-in
        std::string definitionId; // user-defined
    };

    // Collect built-in WebSocket protocols (named community protocols only —
    // exclude the generic "WebSocket" fallback which has no real behaviour).
    std::vector<std::string> builtins;
    for (const auto& p : ProtocolManager::GetInstance().GetAvailableProtocols())
        if (p.find("OSC") == std::string::npos && p != "WebSocket")
            builtins.push_back(p);

    auto buildEntries = [&](ProtocolDirection dir) {
        std::vector<Entry> v;
        for (const auto& p : builtins) { Entry e; e.label = p; e.protocolName = p; v.push_back(e); }
        bool addedSep = false;
        for (const auto& def : ProtocolRegistry::GetInstance().GetDefinitions()) {
            if (def.transport != ProtocolTransport::WebSocket) continue;
            if (def.direction != dir) continue;
            if (!addedSep) { Entry s; s.label = "--- Custom ---"; s.isSeparator = true; v.push_back(s); addedSep = true; }
            Entry e; e.label = def.name; e.isDefinition = true; e.definitionId = def.id; v.push_back(e);
        }
        return v;
    };

    auto findIdx = [&](const std::vector<Entry>& v, const std::string& selDefId) {
        int idx = 0;
        for (int i = 0; i < (int)v.size(); ++i) {
            if (v[i].isSeparator) continue;
            if (v[i].isDefinition && !selDefId.empty() && v[i].definitionId == selDefId) { idx = i; break; }
            if (!v[i].isDefinition && selDefId.empty() && v[i].protocolName == currentProto) { idx = i; }
        }
        return idx;
    };

    auto drawCombo = [&](const char* label, std::vector<Entry>& v, int& curIdx, int& newIdx) {
        const char* preview = v.empty() ? "(none)" : v[curIdx].label.c_str();
        if (ImGui::BeginCombo(label, preview)) {
            for (int i = 0; i < (int)v.size(); ++i) {
                const auto& e = v[i];
                if (e.isSeparator) { ImGui::Separator(); ImGui::TextDisabled("Custom Protocols"); continue; }
                bool sel = (i == curIdx);
                if (ImGui::Selectable(e.label.c_str(), sel)) newIdx = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    };

    // ── Output protocol (server → client) ────────────────────────────────────
    {
        if (ImGui::Checkbox("##ws_out_en", &outputEnabled)) {
            SetOutputEnabled(outputEnabled);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        ImGui::SameLine();
        if (!outputEnabled) ImGui::BeginDisabled();
        auto entries = buildEntries(ProtocolDirection::Output);
        int curIdx = findIdx(entries, outDefId);
        int newIdx = curIdx;
        ImGui::Text("Output (send to clients)");
        drawCombo("##out_proto", entries, curIdx, newIdx);
        if (newIdx != curIdx && !entries[newIdx].isSeparator) {
            const auto& c = entries[newIdx];
            if (c.isDefinition) { SetOutputDefinition(c.definitionId); portInput = GetPort(); }
            else                { SetOutputDefinition(""); SetProtocol(c.protocolName); }
        }
        if (!outDefId.empty() && ProtocolRegistry::GetInstance().FindById(outDefId))
            { ImGui::SameLine(); ImGui::TextDisabled("(port synced)"); }
        if (!outputEnabled) ImGui::EndDisabled();
    }

    // ── Input protocol (client → server) ─────────────────────────────────────
    {
        if (ImGui::Checkbox("##ws_in_en", &inputEnabled)) {
            SetInputEnabled(inputEnabled);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        ImGui::SameLine();
        if (!inputEnabled) ImGui::BeginDisabled();
        auto entries = buildEntries(ProtocolDirection::Input);
        int curIdx = findIdx(entries, inDefId);
        int newIdx = curIdx;
        ImGui::Text("Input (receive from clients)");
        drawCombo("##in_proto", entries, curIdx, newIdx);
        if (newIdx != curIdx && !entries[newIdx].isSeparator) {
            const auto& c = entries[newIdx];
            if (c.isDefinition) SetInputDefinition(c.definitionId);
            else                { SetInputDefinition(""); SetProtocol(c.protocolName); }
        }
        if (!inputEnabled) ImGui::EndDisabled();
    }

    // ── Inactivity Timeout ────────────────────────────────────────────────────
    {
        ImGui::Separator();
        bool timeoutEnabled = m_Impl->inactivityTimeoutEnabled; // Read directly
        if (ImGui::Checkbox("Inactivity Timeout", &timeoutEnabled)) {
            SetInactivityTimeoutEnabled(timeoutEnabled);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        if (!timeoutEnabled) ImGui::BeginDisabled();
        if (ImGui::InputInt("Limit (ms)##ws_timeout", &timeoutMs)) {
            if (timeoutMs < 100) timeoutMs = 100; // Sensible minimum
            SetInactivityTimeoutMs(static_cast<uint64_t>(timeoutMs));
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        if (!timeoutEnabled) ImGui::EndDisabled();
    }

    // ── Status / start / stop ─────────────────────────────────────────────────
    if (running) {
        ImGui::TextColored(ImVec4(0,1,0,1), "Status: Running (Port %d)", runningPort);
        if (runningPort != currentPort) {
            ImGui::SameLine();
            if (restartPending) {
                ImGui::TextDisabled("(Restarting...)");
            } else if (ImGui::Button("Restart to apply")) {
                Stop();
                {
                    std::lock_guard<std::mutex> lock(m_Impl->mutex);
                    m_Impl->restartPending = true; m_Impl->restartPort = currentPort;
                }
                InputMapper::GetInstance().SaveCurrentProfile();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            Stop();
            InputMapper::GetInstance().SaveCurrentProfile();
        }
        ImGui::Separator();
        ImGui::Text("Connected Clients: %d", clientCount);
        if (ImGui::TreeNode("Client List")) {
            if (ImGui::BeginChild("Clients", ImVec2(0, 100), true))
                for (const auto& p : clients) ImGui::TextUnformatted(p.second.c_str());
            ImGui::EndChild(); ImGui::TreePop();
        }
    } else {
        ImGui::TextColored(ImVec4(1,0,0,1), "Status: Stopped");
        ImGui::SameLine();
        if (ImGui::Button("Start")) {
            Start(portInput);
            InputMapper::GetInstance().SaveCurrentProfile();
        }
    }

    ImGui::Separator(); ImGui::Text("Log");
    if (ImGui::BeginChild("Log", ImVec2(0, 150), true)) {
        for (const auto& l : logs) {
            if (l.isError)
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", l.text.c_str());
            else
                ImGui::TextUnformatted(l.text.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void WebSocketServer::SetOutputMapper(OutputMapper* mapper) {
    // Must hold the mutex: m_OutputMapper is read from the uWS event-loop thread
    // in the .message and .close callbacks, so writes from the main thread require
    // the same lock those callbacks use.
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_OutputMapper = mapper;
}

void WebSocketServer::SetPortFromProfile(int port) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->port = port;
}

void WebSocketServer::CheckInactivity() {
    bool timed_out = false;
    OutputMapper* mapper = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        if (!m_Impl->inactivityTimeoutEnabled) return;
        if (m_Impl->running && !m_Impl->clients.empty()) {
            if (m_Impl->lastMessageTime == 0) {
                // Timeout already fired; re-arm so we keep checking on every
                // future frame.  If the client resumes sending, the .message
                // handler will write a fresh tick and normal detection resumes.
                m_Impl->lastMessageTime = SDL_GetTicks();
            } else if (SDL_GetTicks() - m_Impl->lastMessageTime > m_Impl->inactivityTimeoutMs) {
                timed_out = true;
                // Log the timeout message
                m_Impl->logs.push_back({"WebSocket clients timed out (no data). Stopping haptics.", false});
                if (m_Impl->logs.size() > 100) m_Impl->logs.pop_front();

                m_Impl->clients.clear();
                m_Impl->lastMessageTime = 0;

                // m_OutputMapper may be null if Stop() ran during the same frame;
                // savedOutputMapper always holds the last valid pointer.
                mapper = m_OutputMapper ? m_OutputMapper : m_Impl->savedOutputMapper;
            }
        }
    }
    if (timed_out && mapper) {
        mapper->StopAllHapticEffects();
    }
}

bool WebSocketServer::IsOutputEnabled() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->outputEnabled;
}
bool WebSocketServer::IsInputEnabled() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->inputEnabled;
}
void WebSocketServer::SetOutputEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->outputEnabled = enabled;
}
void WebSocketServer::SetInputEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->inputEnabled = enabled;
}

void WebSocketServer::SetInactivityTimeoutEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->inactivityTimeoutEnabled = enabled;
}

void WebSocketServer::SetInactivityTimeoutMs(uint64_t ms) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->inactivityTimeoutMs = ms;
}
#endif