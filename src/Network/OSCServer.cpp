#include "Network/OSCServer.h"
#include "Mappers/OutputMapper.h"
#include "Mappers/InputMapper.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "Protocols/OSCBaseProtocol.h"
#include <SDL3/SDL_timer.h>
#include <iostream>
#include <string>
#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>

static std::atomic<bool> s_isDestroyed{false};

OSCServer& OSCServer::GetInstance() {
    static OSCServer instance;
    return instance;
}

OSCServer::OSCServer() {
    s_isDestroyed = false;
    SetProtocol("OSC Back Ally Racing");
}

OSCServer::~OSCServer() {
    Stop();
    // Join the cleanup thread (if any) so it finishes accessing our members
    // before the destructor body returns and the members are destroyed.
    if (m_cleanupThread.joinable())
        m_cleanupThread.join();
    s_isDestroyed = true;
}

int OSCServer::haptic_rumble_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    // All static handlers are called from the liblo background thread (a plain C thread).
    // Any uncaught C++ exception propagating into that thread is undefined behaviour and
    // will typically terminate the process.  Guard every handler with try/catch.
    // Also check s_isDestroyed to avoid touching the OSCServer object after it has been
    // destroyed (belt-and-suspenders alongside lo_server_thread_stop in Stop()).
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 4) {
            int id = argv[0]->i;
            float low = argv[1]->f;
            float high = argv[2]->f;
            int duration = argv[3]->i;
            {
                std::lock_guard<std::mutex> lk(server->m_mutex);
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Haptic rumble: dev=%d low=%.2f high=%.2f dur=%dms", id, low, high, duration);
                server->m_logs.push_back(buf);
                if (server->m_logs.size() > 100) server->m_logs.pop_front();
            }
            server->m_OutputMapper->QueueRumble(id, low, high, duration);
        }
    } catch (...) {}
    return 0;
}

int OSCServer::haptic_constant_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 3) {
            int id = argv[0]->i;
            float strength = argv[1]->f;
            int duration = argv[2]->i;
            {
                std::lock_guard<std::mutex> lk(server->m_mutex);
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Haptic constant: dev=%d strength=%.2f dur=%dms", id, strength, duration);
                server->m_logs.push_back(buf);
                if (server->m_logs.size() > 100) server->m_logs.pop_front();
            }
            server->m_OutputMapper->QueueConstantForce(id, strength, duration);
        }
    } catch (...) {}
    return 0;
}

int OSCServer::haptic_periodic_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 7) {
            int id = argv[0]->i;
            float strength = argv[1]->f;
            int period = argv[2]->i;
            float magnitude = argv[3]->f;
            float offset = argv[4]->f;
            int phase = argv[5]->i;
            int duration = argv[6]->i;
            {
                std::lock_guard<std::mutex> lk(server->m_mutex);
                char buf[160];
                std::snprintf(buf, sizeof(buf), "Haptic periodic: dev=%d str=%.2f per=%d mag=%.2f dur=%dms", id, strength, period, magnitude, duration);
                server->m_logs.push_back(buf);
                if (server->m_logs.size() > 100) server->m_logs.pop_front();
            }
            server->m_OutputMapper->QueuePeriodic(id, strength, period, magnitude, offset, phase, duration);
        }
    } catch (...) {}
    return 0;
}

int OSCServer::haptic_condition_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 10) {
            int id = argv[0]->i;
            int slot = argv[1]->i;
            uint16_t ctype = (uint16_t)argv[2]->i;
            float rsat = argv[3]->f, lsat = argv[4]->f;
            float rcoeff = argv[5]->f, lcoeff = argv[6]->f;
            float db = argv[7]->f, center = argv[8]->f;
            int duration = argv[9]->i;
            {
                std::lock_guard<std::mutex> lk(server->m_mutex);
                char buf[160];
                std::snprintf(buf, sizeof(buf), "Haptic condition: dev=%d slot=%d type=%u rcoeff=%.2f lcoeff=%.2f dur=%dms", id, slot, ctype, rcoeff, lcoeff, duration);
                server->m_logs.push_back(buf);
                if (server->m_logs.size() > 100) server->m_logs.pop_front();
            }
            server->m_OutputMapper->QueueCondition(id, slot, ctype, rsat, lsat, rcoeff, lcoeff, db, center, duration);
        }
    } catch (...) {}
    return 0;
}

int OSCServer::haptic_gain_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server || !server->m_running || !server->m_OutputMapper) return 0;
        if (argc >= 2) {
            int id = argv[0]->i;
            int gain = argv[1]->i;
            {
                std::lock_guard<std::mutex> lk(server->m_mutex);
                char buf[80];
                std::snprintf(buf, sizeof(buf), "Haptic gain: dev=%d gain=%d", id, gain);
                server->m_logs.push_back(buf);
                if (server->m_logs.size() > 100) server->m_logs.pop_front();
            }
            server->m_OutputMapper->QueueSetGain(id, gain);
        }
    } catch (...) {}
    return 0;
}

bool OSCServer::Start(const std::string& send_host, int send_port, int recv_port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return true;
    }

    m_clients.clear();

    // Update internal state for UI
    strncpy(m_send_host, send_host.c_str(), sizeof(m_send_host) - 1);
    m_send_host[sizeof(m_send_host) - 1] = '\0';
    m_send_port = send_port;
    m_recv_port = recv_port;

    m_running_send_host = send_host;
    m_running_send_port = send_port;
    m_running_recv_port = recv_port;

    // Setup sending address
    m_send_address = lo_address_new(send_host.c_str(), std::to_string(send_port).c_str());
    if (!m_send_address) {
        std::cerr << "OSC Error: Could not create send address " << send_host << ":" << send_port << std::endl;
        return false;
    }

    // Setup receiving server
    std::string recv_port_str = std::to_string(recv_port);
    m_server_thread = lo_server_thread_new_with_proto(recv_port_str.c_str(), LO_UDP, nullptr);
    if (!m_server_thread) {
        std::cerr << "OSC Error: Could not create server on port " << recv_port << std::endl;
        lo_address_free(m_send_address);
        m_send_address = nullptr;
        return false;
    }

    lo_server_thread_add_method(m_server_thread, "/haptic/rumble", "iffi", haptic_rumble_handler, this);
    lo_server_thread_add_method(m_server_thread, "/haptic/constant", "ifi", haptic_constant_handler, this);
    lo_server_thread_add_method(m_server_thread, "/haptic/periodic", "ififfii", haptic_periodic_handler, this);
    lo_server_thread_add_method(m_server_thread, "/haptic/condition", "iiiffffffi", haptic_condition_handler, this);
    lo_server_thread_add_method(m_server_thread, "/haptic/gain", "ii", haptic_gain_handler, this);
    lo_server_thread_add_method(m_server_thread, nullptr, nullptr, generic_handler, this);
    lo_server_thread_start(m_server_thread);

    m_running = true;
    m_isConnected = true;

    // Restore the OutputMapper that was saved when Stop() was last called.
    // Stop() deliberately nulls m_OutputMapper to guard against use-after-free
    // during shutdown, but this leaves it null when the server is restarted from
    // the UI.  Restoring the saved pointer here ensures haptic effects keep
    // working across Stop/Start cycles without requiring the caller to
    // re-call SetOutputMapper().
    if (!m_OutputMapper && m_savedOutputMapper)
        m_OutputMapper = m_savedOutputMapper;

    std::cout << "OSC server started. Sending to " << send_host << ":" << send_port
              << ", Listening on port " << recv_port << std::endl;

    m_logs.push_back("OSC server started. Sending to " + send_host + ":" + std::to_string(send_port) + ", Listening on port " + std::to_string(recv_port));
    if (m_logs.size() > 100) m_logs.pop_front();

    return true;
}

void OSCServer::Stop() {
    lo_server_thread thread_to_stop = nullptr;
    lo_address address_to_free = nullptr;
    OutputMapper* mapper = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            return;
        }

        m_running = false;
        m_isConnected = false;

        thread_to_stop = m_server_thread;
        m_server_thread = nullptr;

        address_to_free = m_send_address;
        m_send_address = nullptr;

        // Clear the client list immediately so the UI shows no clients
        // as soon as Stop() is called, rather than keeping stale entries
        // until the next Start().
        m_clients.clear();

        // Capture and null m_OutputMapper inside the lock.  The static haptic
        // handlers check m_running (atomic) first, but a handler that passed
        // that check before we set m_running=false could still call into
        // m_OutputMapper.  Clearing the pointer here closes that window: any
        // such handler will see nullptr on the next check and bail out safely.
        // We also save a copy in m_savedOutputMapper so that a subsequent
        // Start() call can restore it without requiring an external
        // SetOutputMapper() call (fixing the "effects stop after UI restart" bug).
        mapper = m_OutputMapper;
        m_savedOutputMapper = m_OutputMapper;
        m_OutputMapper = nullptr;
    }

    if (mapper) {
        mapper->StopAllHapticEffects();
    }

    // lo_server_thread_stop() blocks until the liblo receive thread exits.
    // With active clients continuously sending UDP packets this can stall
    // the caller (the UI/main thread) for a noticeable period.  Move the
    // blocking teardown onto a detached thread so the UI stays responsive.
    // Both handles are captured by value; the OSCServer singleton outlives
    // the detached thread, so the final log append is safe.
    if (thread_to_stop || address_to_free) {
        // Join any previous cleanup thread before launching a new one.
        // This prevents overlapping cleanups and ensures the stored thread
        // is always in a joinable or default (not-a-thread) state.
        if (m_cleanupThread.joinable())
            m_cleanupThread.join();

        m_cleanupThread = std::thread([this, thread_to_stop, address_to_free]() {
            if (thread_to_stop) {
                lo_server_thread_stop(thread_to_stop);
                lo_server_thread_free(thread_to_stop);
            }
            if (address_to_free) {
                lo_address_free(address_to_free);
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            std::cout << "OSC server stopped." << std::endl;
            m_logs.push_back("OSC server stopped.");
            if (m_logs.size() > 100) m_logs.pop_front();
        });
    }
}

void OSCServer::WaitStopped() {
    // Block until the liblo cleanup thread (spawned by Stop()) has finished
    // calling lo_server_thread_stop().  Once joined, no more liblo callbacks
    // can fire, so it is safe to destroy objects those callbacks reference
    // (e.g. OutputMapper).
    if (m_cleanupThread.joinable())
        m_cleanupThread.join();
}

bool OSCServer::IsDestroyed() {
    return s_isDestroyed.load();
}

bool OSCServer::IsRunning() const {
    return m_running;
}

void OSCServer::Send(const std::string& path, const char* types, ...) {
    if (s_isDestroyed) return;

    // Avoid sending updates for unbound outputs
    if (!InputMapper::GetInstance().IsOutputAddressBound(path)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || !m_send_address) return;

    va_list ap;
    va_start(ap, types);
    lo_message msg = lo_message_new();
    // Append "$$" to types to bypass liblo's LO_MARKER check, which fails when wrapping varargs
    std::string types_str = (types ? types : "") + std::string("$$");
    lo_message_add_varargs(msg, types_str.c_str(), ap);
    int result = lo_send_message(m_send_address, path.c_str(), msg);
    m_isConnected = (result != -1);
    lo_message_free(msg);
    va_end(ap);
}

static std::string formatFloat(float val, int precision) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, val);
    std::string s(buf);
    std::replace(s.begin(), s.end(), ',', '.');
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.')
        s.pop_back();
    return s;
}

void OSCServer::SendWheel(float steer, float brake, float throttle, float pitch, float roll) {
    // Currently all OSC protocols use the same sending logic in this server implementation
    // Ideally this would delegate to m_protocol->format_wheel, but we need to handle binary bundles.
    // For now, we keep the logic here but it applies to all selected OSC protocols.
    if (m_protocol) {
        Send("/wheel/steer", "f", steer);
        Send("/wheel/brake", "f", brake);
        Send("/wheel/throttle", "f", throttle);
        Send("/wheel/pitch", "f", pitch);
        Send("/wheel/roll", "f", roll);
    }
}

void OSCServer::SendButtons(const std::vector<uint32_t>& buttons) {
    int b0 = buttons.size() > 0 ? static_cast<int>(buttons[0]) : 0;
    int b1 = buttons.size() > 1 ? static_cast<int>(buttons[1]) : 0;
    int b2 = buttons.size() > 2 ? static_cast<int>(buttons[2]) : 0;
    int b3 = buttons.size() > 3 ? static_cast<int>(buttons[3]) : 0;

    if (m_protocol) {
        Send("/wheel/buttons/0", "i", b0);
        Send("/wheel/buttons/1", "i", b1);
        Send("/wheel/buttons/2", "i", b2);
        Send("/wheel/buttons/3", "i", b3);
    }
}

void OSCServer::SetHandler(OSCHandler handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handler = std::move(handler);
}

int OSCServer::generic_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data) {
    try {
        if (s_isDestroyed) return 0;
        auto* server = static_cast<OSCServer*>(user_data);
        if (!server) return 0;

        // --- Snapshot all state we need under the lock, then release it.
        // Holding m_mutex while doing protocol dispatch or ProtocolManager calls
        // can stall Send(), Stop(), and the UI thread, and creates a lock-order
        // dependency (m_mutex → ProtocolManager::m_mutex) that can deadlock if
        // the main thread ever takes those locks in the opposite order.
        std::shared_ptr<IProtocol>  protoCopy;
        OSCHandler                  handlerCopy;
        bool                        isRunning;
        std::string                 legacyInputProto;

        {
            std::lock_guard<std::mutex> lock(server->m_mutex);
            server->m_lastMessageTime = SDL_GetTicks();
            isRunning  = server->m_running;

            // Track the source client while we still hold the lock
            lo_address src = lo_message_get_source(msg);
            if (src) {
                const char* hostname = lo_address_get_hostname(src);
                const char* port     = lo_address_get_port(src);
                if (hostname && port) {
                    server->m_clients.insert(std::string(hostname) + ":" + std::string(port));
                }
            }

            server->m_logs.push_back("Recv: " + std::string(path));
            if (server->m_logs.size() > 100) server->m_logs.pop_front();

            protoCopy    = server->m_protocol;
            handlerCopy  = server->m_handler;
        }
        // m_mutex is now released — safe to do slow work below.

        if (!isRunning) return 0;

        // Resolve the active legacy input protocol outside the lock so we don't
        // hold m_mutex while calling into ProtocolManager.
        legacyInputProto = ProtocolManager::GetInstance().GetActiveInputLegacyProtocol();
        if (!legacyInputProto.empty()) {
            auto p = ProtocolManager::GetInstance().GetProtocol(legacyInputProto);
            if (p) protoCopy = p;
        }

        if (protoCopy) {
            auto oscProtocol = std::dynamic_pointer_cast<OSCBaseProtocol>(protoCopy);
            if (oscProtocol) {
                oscProtocol->handle_osc_message(path, types, argv, argc);
            }
        } else if (handlerCopy) {
            handlerCopy(path, types, argv, argc);
        }
    } catch (...) {}
    return 0;
}

void OSCServer::SetProtocol(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_protocol = ProtocolManager::GetInstance().GetProtocol(name);
    if (m_protocol) m_protocolName = name;
}

std::string OSCServer::GetProtocol() const {
    return m_protocolName;
}

void OSCServer::SetDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selectedDefinitionId = definitionId;

    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) {
            // Sync host/port from the definition into the UI fields
            strncpy(m_send_host, def->oscHost.c_str(), sizeof(m_send_host) - 1);
            m_send_host[sizeof(m_send_host) - 1] = '\0';
            m_send_port = def->oscSendPort;
            m_recv_port = def->oscRecvPort;
        }
    }
}

std::string OSCServer::GetDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selectedDefinitionId;
}

void OSCServer::SetOutputDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputDefinitionId = definitionId;
    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) {
            strncpy(m_send_host, def->oscHost.c_str(), sizeof(m_send_host) - 1);
            m_send_host[sizeof(m_send_host) - 1] = '\0';
            m_send_port = def->oscSendPort;
        }
    }
}

void OSCServer::SetInputDefinition(const std::string& definitionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inputDefinitionId = definitionId;
    if (!definitionId.empty()) {
        const auto* def = ProtocolRegistry::GetInstance().FindById(definitionId);
        if (def) m_recv_port = def->oscRecvPort;
    }
}

std::string OSCServer::GetOutputDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_outputDefinitionId;
}

std::string OSCServer::GetInputDefinitionId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inputDefinitionId;
}

void OSCServer::LoadConfig(const PreferencesManager& prefs) {
    std::string send_host = prefs.GetString("OSC", "SendHost", "127.0.0.1");
    int send_port = prefs.GetInt("OSC", "SendPort", 9066);
    int recv_port = prefs.GetInt("OSC", "RecvPort", 9068);
    std::string protocol   = prefs.GetString("OSC", "Protocol", "OSC Back Ally Racing");
    std::string inputProtocol = prefs.GetString("OSC", "InputProtocol", "");
    std::string outDefId   = prefs.GetString("OSC", "OutputDefinitionId", "");
    std::string inDefId    = prefs.GetString("OSC", "InputDefinitionId",  "");
    bool enabled = prefs.GetBool("OSC", "Enabled", false);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        strncpy(m_send_host, send_host.c_str(), sizeof(m_send_host) - 1);
        m_send_host[sizeof(m_send_host) - 1] = '\0';
        m_send_port = send_port;
        m_recv_port = recv_port;
    }

    SetProtocol(protocol);

    if (!inputProtocol.empty()) ProtocolManager::GetInstance().SetActiveInputLegacyProtocol(inputProtocol);
    else ProtocolManager::GetInstance().SetActiveInputLegacyProtocol(protocol);

    if (!outDefId.empty()) SetOutputDefinition(outDefId);
    if (!inDefId.empty())  SetInputDefinition(inDefId);

    if (enabled) {
        Start(send_host, send_port, recv_port);
    }
}

void OSCServer::SaveConfig(PreferencesManager& prefs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    prefs.SetString("OSC", "SendHost",           m_send_host);
    prefs.SetInt   ("OSC", "SendPort",            m_send_port);
    prefs.SetInt   ("OSC", "ReceivePort",         m_recv_port);
    prefs.SetString("OSC", "Protocol",            m_protocolName);
    prefs.SetString("OSC", "InputProtocol",       ProtocolManager::GetInstance().GetActiveInputLegacyProtocol());
    prefs.SetString("OSC", "OutputDefinitionId",  m_outputDefinitionId);
    prefs.SetString("OSC", "InputDefinitionId",   m_inputDefinitionId);
    prefs.SetBool  ("OSC", "Enabled",             m_running);
}

void OSCServer::SetSelectedDevice(int id) {
    m_selectedDeviceId = id;
}

int OSCServer::GetSelectedDevice() const {
    return m_selectedDeviceId;
}

const char* OSCServer::GetSendHost() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_send_host;
}

int OSCServer::GetSendPort() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_send_port;
}

int OSCServer::GetReceivePort() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_recv_port;
}

void OSCServer::DrawContent() {
    // --- Client timeout logic --------------------------------------------------
    const uint64_t OSC_CLIENT_TIMEOUT_MS = 5000; // 5 seconds
    bool clients_timed_out = false;
    OutputMapper* mapper = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running && !m_clients.empty() && m_lastMessageTime > 0 && (SDL_GetTicks() - m_lastMessageTime > OSC_CLIENT_TIMEOUT_MS)) {
            clients_timed_out = true;
            m_clients.clear();
            m_lastMessageTime = 0; // Prevent re-triggering
            m_logs.push_back("OSC clients timed out. Stopping haptics.");
            if (m_logs.size() > 100) m_logs.pop_front();
            mapper = m_OutputMapper;
        }
    }
    if (clients_timed_out && mapper) {
        mapper->StopAllHapticEffects();
    }

    ImGui::InputInt("Send Port",    &m_send_port);
    ImGui::InputInt("Receive Port", &m_recv_port);

    // ── Read state under lock ─────────────────────────────────────────────────
    std::string outDefId, inDefId, currentProto;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        outDefId     = m_outputDefinitionId;
        inDefId      = m_inputDefinitionId;
        currentProto = m_protocolName;
    }

    std::string currentInputProto = ProtocolManager::GetInstance().GetActiveInputLegacyProtocol();
    if (currentInputProto.empty()) currentInputProto = currentProto;

    // ── Combo helpers ─────────────────────────────────────────────────────────
    struct Entry {
        std::string label;
        bool isSeparator  = false;
        bool isDefinition = false;
        std::string protocolName;
        std::string definitionId;
    };

    // Built-in OSC protocols
    std::vector<std::string> builtins;
    for (const auto& p : ProtocolManager::GetInstance().GetAvailableProtocols())
        if (p.find("OSC") != std::string::npos)
            builtins.push_back(p);

    auto buildEntries = [&](ProtocolDirection dir) {
        std::vector<Entry> v;
        for (const auto& p : builtins) { Entry e; e.label = p; e.protocolName = p; v.push_back(e); }
        bool addedSep = false;
        for (const auto& def : ProtocolRegistry::GetInstance().GetDefinitions()) {
            if (def.transport != ProtocolTransport::OSC) continue;
            if (def.direction != dir) continue;
            if (!addedSep) { Entry s; s.label = "--- Custom ---"; s.isSeparator = true; v.push_back(s); addedSep = true; }
            Entry e; e.label = def.name; e.isDefinition = true; e.definitionId = def.id; v.push_back(e);
        }
        return v;
    };

    auto findIdx = [&](const std::vector<Entry>& v, const std::string& selDefId, const std::string& selProto) {
        int idx = 0;
        for (int i = 0; i < (int)v.size(); ++i) {
            if (v[i].isSeparator) continue;
            if (v[i].isDefinition && !selDefId.empty() && v[i].definitionId == selDefId) { idx = i; break; }
            if (!v[i].isDefinition && selDefId.empty() && v[i].protocolName == selProto) { idx = i; }
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

    // ── Output protocol (server → client) ─────────────────────────────────────
    {
        auto entries = buildEntries(ProtocolDirection::Output);
        int curIdx = findIdx(entries, outDefId, currentProto);
        int newIdx = curIdx;
        ImGui::Text("Output (send to client)");
        drawCombo("##osc_out", entries, curIdx, newIdx);
        if (newIdx != curIdx && !entries[newIdx].isSeparator) {
            const auto& c = entries[newIdx];
            if (c.isDefinition) SetOutputDefinition(c.definitionId);
            else                { SetOutputDefinition(""); SetProtocol(c.protocolName); }
        }
        if (!outDefId.empty() && ProtocolRegistry::GetInstance().FindById(outDefId))
            { ImGui::SameLine(); ImGui::TextDisabled("(ports synced)"); }
    }

    // ── Input protocol (client → server) ──────────────────────────────────────
    {
        auto entries = buildEntries(ProtocolDirection::Input);
        int curIdx = findIdx(entries, inDefId, currentInputProto);
        int newIdx = curIdx;
        ImGui::Text("Input (receive from client)");
        drawCombo("##osc_in", entries, curIdx, newIdx);
        if (newIdx != curIdx && !entries[newIdx].isSeparator) {
            const auto& c = entries[newIdx];
            if (c.isDefinition) {
                SetInputDefinition(c.definitionId);
                ProtocolManager::GetInstance().SetActiveInputLegacyProtocol("");
            }
            else { SetInputDefinition(""); ProtocolManager::GetInstance().SetActiveInputLegacyProtocol(c.protocolName); }
        }
        if (!inDefId.empty() && ProtocolRegistry::GetInstance().FindById(inDefId))
            { ImGui::SameLine(); ImGui::TextDisabled("(recv port synced)"); }
    }

    // ── Start / stop ──────────────────────────────────────────────────────────
    if (IsRunning()) {
        bool settingsChanged = (m_send_port != m_running_send_port) ||
                               (m_recv_port != m_running_recv_port) ||
                               (m_running_send_host != m_send_host);
        if (settingsChanged) {
            if (ImGui::Button("Restart to apply")) {
                Stop();
                Start(m_send_host, m_send_port, m_recv_port);
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Stop OSC")) Stop();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0,1,0,1), "Running");
        ImGui::SameLine();
        ImGui::TextColored(m_isConnected ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
                           m_isConnected ? "Connected" : "Send Error");
    } else {
        if (ImGui::Button("Start OSC")) Start(m_send_host, m_send_port, m_recv_port);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,0,0,1), "Stopped");
    }

    std::deque<std::string> logs;
    std::set<std::string> clients;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        logs    = m_logs;
        clients = m_clients;
    }

    ImGui::Separator();
    ImGui::Text("Known Clients (Sources): %d", (int)clients.size());
    if (ImGui::TreeNode("Client List")) {
        if (ImGui::BeginChild("Clients", ImVec2(0, 100), true))
            for (const auto& c : clients) ImGui::TextUnformatted(c.c_str());
        ImGui::EndChild();
        ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::Text("Log");
    if (ImGui::BeginChild("Log", ImVec2(0, 150), true)) {
        for (const auto& l : logs) ImGui::TextUnformatted(l.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void OSCServer::SetOutputMapper(OutputMapper* mapper) {
    // Protect with the mutex: the static haptic handlers read m_OutputMapper
    // from the liblo receive thread, so writes from the main thread must be
    // synchronised to prevent a data race.
    std::lock_guard<std::mutex> lock(m_mutex);
    m_OutputMapper = mapper;
}