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

struct us_listen_socket_t;

struct WebSocketServer::Impl {
    int port = 4269;
    bool running = false;
    int runningPort = 0;
    bool restartPending = false;
    int restartPort = 0;
    std::mutex mutex;
    std::deque<std::string> logs;
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
};

WebSocketServer &WebSocketServer::GetInstance() {
    static WebSocketServer instance;
    return instance;
}

WebSocketServer::WebSocketServer() : m_selectedDeviceId(0), m_Impl(new Impl) {
    // Default to Marsmaantje (New)
    SetProtocol("Marsmaantje (New)");
}

WebSocketServer::~WebSocketServer() {
    Stop();
    if (m_Impl->thread.joinable()) {
        m_Impl->thread.join();
    }
    delete m_Impl;
}

void WebSocketServer::Start(int port) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    if (m_Impl->running)
        return;

    m_Impl->port = port;
    m_Impl->running = true;
    m_Impl->runningPort = port;

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
                                   m_Impl->logs.push_back("Client connected: " + ip);
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
                                   {
                                       std::lock_guard<std::mutex> lock(m_Impl->mutex);
                                       m_Impl->logs.push_back("Client data: " + std::string(message));
                                       if (m_Impl->logs.size() > 100)
                                           m_Impl->logs.pop_front();
                                       protoCopy = m_Impl->protocol;
                                       mapperCopy = m_OutputMapper;
                                   }
                                   // Mutex is released — do slow work now.

                                   if (protoCopy) {
                                       protoCopy->parse(std::string(message));
                                   }

                                   // Also parse for generic haptic commands
                                   HapticParser::Parse(message, mapperCopy);
                               },
                           .close =
                               [this](auto *ws, int code, std::string_view message) {
                                   bool doStopHaptics = false;
                                   OutputMapper* mapperCopy = nullptr;
                                   {
                                       std::lock_guard<std::mutex> lock(m_Impl->mutex);
                                       if (m_Impl->clients.count(ws)) {
                                           m_Impl->logs.push_back("Client disconnected: " + m_Impl->clients[ws]);
                                           if (m_Impl->logs.size() > 100)
                                               m_Impl->logs.pop_front();
                                           m_Impl->clients.erase(ws);

                                           if (m_Impl->clients.empty()) {
                                               doStopHaptics = true;
                                               mapperCopy = m_OutputMapper;
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
                            m_Impl->logs.push_back("WebSocket server listening on port " + std::to_string(m_Impl->port));
                            m_Impl->listen_socket = (struct us_listen_socket_t *)listen_socket;
                        } else {
                            m_Impl->logs.push_back("Failed to listen on port " + std::to_string(m_Impl->port));
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
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->restartPending = false;

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

    if (m_OutputMapper) {
        m_OutputMapper->StopAllHapticEffects();
    }
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
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->protocol = ProtocolManager::GetInstance().GetProtocol(name);
    if (m_Impl->protocol) m_Impl->selectedProtocol = name;
}

std::string WebSocketServer::GetProtocol() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->selectedProtocol;
}

void WebSocketServer::SetDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->selectedDefinitionId = definitionId;

    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) {
            // Sync port from the definition into the UI field
            m_Impl->port = def->wssPort;
        }
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
        uWS::OpCode opCode = (protocol->getProtocolName() == "OSC") ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
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
        uWS::OpCode opCode = (protocol->getProtocolName() == "OSC") ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
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
        uWS::OpCode opCode = (protocol->getProtocolName() == "OSC") ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
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
            uWS::OpCode opCode = (protocol->getProtocolName() == "OSC") ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
            Broadcast(msg, opCode);
        }
    }
}

void WebSocketServer::LoadConfig(const PreferencesManager& prefs) {
    int port = prefs.GetInt("WebSocket", "Port", 4269);
    std::string protocol = prefs.GetString("WebSocket", "Protocol", "Marsmaantje (New)");
    std::string outDefId = prefs.GetString("WebSocket", "OutputDefinitionId", "");
    std::string inDefId  = prefs.GetString("WebSocket", "InputDefinitionId",  "");
    bool enabled = prefs.GetBool("WebSocket", "Enabled", false);

    SetPort(port);
    SetProtocol(protocol);

    if (!outDefId.empty()) SetOutputDefinition(outDefId);
    if (!inDefId.empty())  SetInputDefinition(inDefId);

    if (enabled) Start(port);
}

void WebSocketServer::SaveConfig(PreferencesManager& prefs) {
    int port;
    std::string protocol, outDef, inDef;
    bool running;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        port     = m_Impl->port;
        running  = m_Impl->running;
        protocol = m_Impl->selectedProtocol;
        outDef   = m_Impl->outputDefinitionId;
        inDef    = m_Impl->inputDefinitionId;
    }
    prefs.SetInt("WebSocket",    "Port",               port);
    prefs.SetString("WebSocket", "Protocol",           protocol);
    prefs.SetString("WebSocket", "OutputDefinitionId", outDef);
    prefs.SetString("WebSocket", "InputDefinitionId",  inDef);
    prefs.SetBool("WebSocket",   "Enabled",            running);
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
    int  currentPort, runningPort, clientCount;
    std::string outDefId, inDefId, currentProto;
    std::deque<std::string> logs;
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

    // Collect built-in WebSocket protocols once
    std::vector<std::string> builtins;
    for (const auto& p : ProtocolManager::GetInstance().GetAvailableProtocols())
        if (p.find("OSC") == std::string::npos)
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
    }

    // ── Input protocol (client → server) ─────────────────────────────────────
    {
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
                std::lock_guard<std::mutex> lock(m_Impl->mutex);
                m_Impl->restartPending = true; m_Impl->restartPort = currentPort;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) Stop();
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
        if (ImGui::Button("Start")) Start(portInput);
    }

    ImGui::Separator(); ImGui::Text("Log");
    if (ImGui::BeginChild("Log", ImVec2(0, 150), true)) {
        for (const auto& l : logs) ImGui::TextUnformatted(l.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void WebSocketServer::SetOutputMapper(OutputMapper* mapper) {
    m_OutputMapper = mapper;
}
#endif
