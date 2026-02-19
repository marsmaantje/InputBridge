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
        if (server->m_protocol) {
            auto oscProtocol = std::dynamic_pointer_cast<OSCBaseProtocol>(server->m_protocol);
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

void OSCServer::LoadConfig(const PreferencesManager& prefs) {
    std::string send_host = prefs.GetString("OSC", "SendHost", "127.0.0.1");
    int send_port = prefs.GetInt("OSC", "SendPort", 9066);
    int recv_port = prefs.GetInt("OSC", "RecvPort", 9068);
    std::string protocol = prefs.GetString("OSC", "Protocol", "OSC Default");
    std::string defId = prefs.GetString("OSC", "DefinitionId", "");
    bool enabled = prefs.GetBool("OSC", "Enabled", false);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        strncpy(m_send_host, send_host.c_str(), sizeof(m_send_host) - 1);
        m_send_host[sizeof(m_send_host) - 1] = '\0';
        m_send_port = send_port;
        m_recv_port = recv_port;
    }

    SetProtocol(protocol);

    // Restore definition selection (ProtocolRegistry must be loaded first)
    if (!defId.empty()) {
        SetDefinition(defId);
    }

    if (enabled) {
        Start(send_host, send_port, recv_port);
    }
}

void OSCServer::SaveConfig(PreferencesManager& prefs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    prefs.SetString("OSC", "SendHost", m_send_host);
    prefs.SetInt("OSC", "SendPort", m_send_port);
    prefs.SetInt("OSC", "ReceivePort", m_recv_port);
    prefs.SetString("OSC", "Protocol", m_protocolName);
    prefs.SetString("OSC", "DefinitionId", m_selectedDefinitionId);
    prefs.SetBool("OSC", "Enabled", m_running);
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
    ImGui::InputInt("Send Port", &m_send_port);
    ImGui::InputInt("Receive Port", &m_recv_port);

    // ── Read current selection state under lock ───────────────────────────────
    std::string currentDefId, currentProtoName;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        currentDefId     = m_selectedDefinitionId;
        currentProtoName = m_protocolName;
    }

    // ── Build combo entries ───────────────────────────────────────────────────
    // Each entry carries enough info to act on selection.
    // isDefinition==false  → built-in protocol (protocolName non-empty)
    //                         or visual separator (protocolName empty)
    // isDefinition==true   → user-defined ProtocolRegistry definition
    struct ComboEntry {
        std::string displayName;
        bool        isDefinition = false;
        bool        isSeparator  = false;
        std::string protocolName;
        std::string definitionId;
    };
    std::vector<ComboEntry> entries;

    // Group 1: built-in OSC protocols from ProtocolManager
    for (const auto& p : ProtocolManager::GetInstance().GetAvailableProtocols()) {
        if (p.find("OSC") != std::string::npos) {
            ComboEntry e;
            e.displayName  = p;
            e.protocolName = p;
            entries.push_back(e);
        }
    }

    // Group 2: user-defined OSC definitions from ProtocolRegistry
    {
        const auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
        bool addedSep = false;
        for (const auto& def : defs) {
            if (def.transport != ProtocolTransport::OSC) continue;
            if (!addedSep) {
                ComboEntry sep;
                sep.displayName  = "--- Custom ---";
                sep.isSeparator  = true;
                entries.push_back(sep);
                addedSep = true;
            }
            ComboEntry e;
            e.displayName  = def.name +
                             (def.direction == ProtocolDirection::Output ? " [Out]" : " [In]");
            e.isDefinition = true;
            e.definitionId = def.id;
            entries.push_back(e);
        }
    }

    // ── Find current selection index ──────────────────────────────────────────
    int currentIdx = 0;
    for (int i = 0; i < (int)entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.isSeparator) continue;
        if (e.isDefinition && !currentDefId.empty() && e.definitionId == currentDefId) {
            currentIdx = i;
            break;
        }
        if (!e.isDefinition && currentDefId.empty() && e.protocolName == currentProtoName) {
            currentIdx = i;
            // don't break – a definition match above takes priority
        }
    }

    // ── Draw combo ────────────────────────────────────────────────────────────
    int newIdx = currentIdx;
    if (ImGui::BeginCombo("Protocol", entries.empty() ? "" : entries[currentIdx].displayName.c_str())) {
        for (int i = 0; i < (int)entries.size(); ++i) {
            const auto& e = entries[i];
            if (e.isSeparator) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", e.displayName.c_str());
                continue;
            }
            bool selected = (i == currentIdx);
            if (ImGui::Selectable(e.displayName.c_str(), selected)) {
                newIdx = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // ── Apply selection if changed ────────────────────────────────────────────
    if (newIdx != currentIdx && newIdx >= 0 && newIdx < (int)entries.size()) {
        const auto& chosen = entries[newIdx];
        if (!chosen.isSeparator) {
            if (chosen.isDefinition) {
                SetDefinition(chosen.definitionId);
            } else {
                SetDefinition("");
                SetProtocol(chosen.protocolName);
            }
        }
    }

    // Hint when a user-defined definition is active
    if (!currentDefId.empty()) {
        if (const auto* def = ProtocolRegistry::GetInstance().FindById(currentDefId)) {
            ImGui::SameLine();
            ImGui::TextDisabled("(ports from definition)");
        }
    }

    // ── Start / stop controls ─────────────────────────────────────────────────
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
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Running");
        ImGui::SameLine();
        ImGui::TextColored(m_isConnected ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
                           m_isConnected ? "Connected" : "Send Error");
    } else {
        if (ImGui::Button("Start OSC")) Start(m_send_host, m_send_port, m_recv_port);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Stopped");
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
        if (ImGui::BeginChild("Clients", ImVec2(0, 100), true)) {
            for (const auto& c : clients) ImGui::TextUnformatted(c.c_str());
        }
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
