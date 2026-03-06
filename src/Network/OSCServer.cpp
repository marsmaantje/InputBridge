#include "Network/OSCServer.h"
#include "Mappers/OutputMapper.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "Protocols/OSCBaseProtocol.h"
#include <iostream>
#include <string>
#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>

OSCServer& OSCServer::GetInstance() {
    static OSCServer instance;
    return instance;
}

OSCServer::OSCServer() {
    SetProtocol("OSC Default");
}

OSCServer::~OSCServer() {
    Stop();
}

int OSCServer::haptic_rumble_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    auto* server = (OSCServer*)user_data;
    if (server->m_OutputMapper && argc >= 4) {
        int id = argv[0]->i;
        float low = argv[1]->f;
        float high = argv[2]->f;
        int duration = argv[3]->i;
        server->m_OutputMapper->QueueRumble(id, low, high, duration);
    }
    return 0;
}

int OSCServer::haptic_constant_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    auto* server = (OSCServer*)user_data;
    if (server->m_OutputMapper && argc >= 3) {
        server->m_OutputMapper->QueueConstantForce(argv[0]->i, argv[1]->f, argv[2]->i);
    }
    return 0;
}

int OSCServer::haptic_periodic_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    auto* server = (OSCServer*)user_data;
    if (server->m_OutputMapper && argc >= 7) {
        server->m_OutputMapper->QueuePeriodic(argv[0]->i, argv[1]->f, argv[2]->i, argv[3]->f, argv[4]->f, argv[5]->i, argv[6]->i);
    }
    return 0;
}

int OSCServer::haptic_condition_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    auto* server = (OSCServer*)user_data;
    if (server->m_OutputMapper && argc >= 8) {
        server->m_OutputMapper->QueueCondition(argv[0]->i, argv[1]->f, argv[2]->f, argv[3]->f, argv[4]->f, argv[5]->f, argv[6]->f, argv[7]->i);
    }
    return 0;
}

int OSCServer::haptic_gain_handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data) {
    auto* server = (OSCServer*)user_data;
    if (server->m_OutputMapper && argc >= 2) {
        server->m_OutputMapper->QueueSetGain(argv[0]->i, argv[1]->i);
    }
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
    lo_server_thread_add_method(m_server_thread, "/haptic/condition", "iffffffi", haptic_condition_handler, this);
    lo_server_thread_add_method(m_server_thread, "/haptic/gain", "ii", haptic_gain_handler, this);
    lo_server_thread_add_method(m_server_thread, nullptr, nullptr, generic_handler, this);
    lo_server_thread_start(m_server_thread);

    m_running = true;
    m_isConnected = true;
    std::cout << "OSC server started. Sending to " << send_host << ":" << send_port
              << ", Listening on port " << recv_port << std::endl;

    m_logs.push_back("OSC server started. Sending to " + send_host + ":" + std::to_string(send_port) + ", Listening on port " + std::to_string(recv_port));
    if (m_logs.size() > 100) m_logs.pop_front();

    return true;
}

void OSCServer::Stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running) {
        return;
    }

    m_running = false;
    m_isConnected = false;

    if (m_server_thread) {
        lo_server_thread_stop(m_server_thread);
        lo_server_thread_free(m_server_thread);
        m_server_thread = nullptr;
    }

    if (m_send_address) {
        lo_address_free(m_send_address);
        m_send_address = nullptr;
    }
    std::cout << "OSC server stopped." << std::endl;
    m_logs.push_back("OSC server stopped.");
    if (m_logs.size() > 100) m_logs.pop_front();
}

bool OSCServer::IsRunning() const {
    return m_running;
}

void OSCServer::Send(const std::string& path, const char* types, ...) {
    if (!m_running) return;
    // Note: We assume Stop() is not called concurrently with Send() for simplicity,
    // or that m_send_address access is safe enough for this context.
    if (!m_send_address) return;

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
    auto* server = static_cast<OSCServer*>(user_data);
    if (server) {
        std::lock_guard<std::mutex> lock(server->m_mutex);

        lo_address src = lo_message_get_source(msg);
        if (src) {
            const char* hostname = lo_address_get_hostname(src);
            const char* port = lo_address_get_port(src);
            if (hostname && port) {
                std::string client = std::string(hostname) + ":" + std::string(port);
                server->m_clients.insert(client);
            }
        }

        server->m_logs.push_back("Recv: " + std::string(path));
        if (server->m_logs.size() > 100) server->m_logs.pop_front();

        // Delegate to protocol if it's an OSC protocol
        std::shared_ptr<IProtocol> proto = server->m_protocol;
        std::string legacyInput = ProtocolManager::GetInstance().GetActiveInputLegacyProtocol();
        if (!legacyInput.empty()) {
            auto p = ProtocolManager::GetInstance().GetProtocol(legacyInput);
            if (p) proto = p;
        }

        if (proto) {
            auto oscProtocol = std::dynamic_pointer_cast<OSCBaseProtocol>(proto);
            if (oscProtocol) {
                oscProtocol->handle_osc_message(path, types, argv, argc);
            }
        } else if (server->m_handler) {
             server->m_handler(path, types, argv, argc);
        }
    }
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
    std::string protocol   = prefs.GetString("OSC", "Protocol", "OSC Default");
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
    m_OutputMapper = mapper;
}
