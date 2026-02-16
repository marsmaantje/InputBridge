#include "WebSocketServer.h"
#include "Preferences/Preferences.h"
#include "../Mappers/OutputMapper.h"
#include <nlohmann/json.hpp>

#if ENABLE_WEBSOCKETS

#include "Protocols/ProtocolManager.h"
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
                                   std::lock_guard<std::mutex> lock(m_Impl->mutex);
                                   m_Impl->logs.push_back("Client data: " + std::string(message));
                                   if (m_Impl->protocol) {
                                       m_Impl->protocol->parse(std::string(message));
                                   }
                                   // Echo the message back to C#
                                   // ProtocolManager::GetInstance().GetProtocol("WebSocket")->parse(std::string(message));
                                   // Note: The above line was problematic if "WebSocket" protocol doesn't exist.
                                   // We rely on m_Impl->protocol now.

                                   if (m_OutputMapper) {
                                       try {
                                           auto json = nlohmann::json::parse(message);
                                           std::string type = json.value("type", "");
                                           if (type == "haptic" || type == "gamepad" || type == "steering_wheel") {
                                               std::string effect = json.value("effect", "");
                                               int device = json.value("device", 0);

                                               nlohmann::json data;
                                               if (json.contains("params")) {
                                                   data = json["params"];
                                               } else if (json.contains("data")) {
                                                   data = json["data"];
                                               } else {
                                                   data = nlohmann::json::object();
                                               }

                                               if (effect == "rumble") {
                                                   float low = data.value("low", 0.0f);
                                                   if (data.contains("large_magnitude")) low = data.value("large_magnitude", 0.0f);

                                                   float high = data.value("high", 0.0f);
                                                   if (data.contains("small_magnitude")) high = data.value("small_magnitude", 0.0f);

                                                   int duration = data.value("duration", 0);
                                                   if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                                                   m_OutputMapper->QueueRumble(device, low, high, duration);
                                               } else if (effect == "constant") {
                                                   float strength = data.value("strength", 0.0f);
                                                   int duration = data.value("duration", 0);
                                                   if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                                                   m_OutputMapper->QueueConstantForce(device, strength, duration);
                                               } else if (effect == "periodic") {
                                                   int duration = data.value("duration", 0);
                                                   if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                                                   m_OutputMapper->QueuePeriodic(device, data.value("strength", 0.0f), data.value("period", 0), data.value("magnitude", 0.0f), data.value("offset", 0.0f), data.value("phase", 0), duration);
                                               } else if (effect == "condition") {
                                                   int duration = data.value("duration", 0);
                                                   if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                                                   m_OutputMapper->QueueCondition(device, data.value("right_sat", 0.0f), data.value("left_sat", 0.0f), data.value("right_coeff", 0.0f), data.value("left_coeff", 0.0f), data.value("deadband", 0.0f), data.value("center", 0.0f), duration);
                                               }
                                           }
                                       } catch (...) {}
                                   }
                               },
                           .close =
                               [this](auto *ws, int code, std::string_view message) {
                                   std::lock_guard<std::mutex> lock(m_Impl->mutex);
                                   if (m_Impl->clients.count(ws)) {
                                       m_Impl->logs.push_back("Client disconnected: " + m_Impl->clients[ws]);
                                       if (m_Impl->logs.size() > 100)
                                           m_Impl->logs.pop_front();
                                       m_Impl->clients.erase(ws);
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
                // Cast back to the specific WebSocket type used in Start()
                auto *ws = (uWS::WebSocket<false, true, int> *)ptr;

                /* Direct send instead of publish */
                ws->send(msg, opCode);
            }
        });
    }
}

void WebSocketServer::Broadcast(const std::string &address, float value) {
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
        std::string msg = protocol->format_wheel(wheel, brake, throttle, pitch, roll);
        if (!msg.empty()) {
            uWS::OpCode opCode = (protocol->getProtocolName() == "OSC") ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
            Broadcast(msg, opCode);
        }
    }
}

void WebSocketServer::LoadConfig(const PreferencesManager& prefs) {
    int port = prefs.GetInt("WebSocket", "Port", 4269);
    std::string protocol = prefs.GetString("WebSocket", "Protocol", "Marsmaantje (New)");
    bool enabled = prefs.GetBool("WebSocket", "Enabled", false);

    SetPort(port);
    SetProtocol(protocol);

    if (enabled) {
        Start(port);
    }
}

void WebSocketServer::SaveConfig(PreferencesManager& prefs) {
    int port;
    std::string protocol;
    bool running;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        port = m_Impl->port;
        running = m_Impl->running;
        protocol = m_Impl->selectedProtocol;
    }
    prefs.SetInt("WebSocket", "Port", port);
    prefs.SetString("WebSocket", "Protocol", protocol);
    prefs.SetBool("WebSocket", "Enabled", running);
}

void WebSocketServer::DrawContent() {
    bool doRestart = false;
    int restartPort = 0;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        if (m_Impl->restartPending && !m_Impl->running) {
            doRestart = true;
            restartPort = m_Impl->restartPort;
            m_Impl->restartPending = false;
        }
    }

    if (doRestart) {
        Start(restartPort);
    }

    bool running;
    int currentPort;
    int clientCount;
    int runningPort;
    bool restartPending;
    std::deque<std::string> logs;
    std::map<void *, std::string> clients;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        running = m_Impl->running;
        currentPort = m_Impl->port;
        clientCount = (int)m_Impl->clients.size();
        runningPort = m_Impl->runningPort;
        restartPending = m_Impl->restartPending;
        logs = m_Impl->logs;
        clients = m_Impl->clients;
    }

    int portInput = currentPort;
    if (ImGui::InputInt("Port", &portInput)) {
        SetPort(portInput);
    }

    // WebSocket Format selection
    std::string newProtocol;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        std::vector<std::string> all_protocols = ProtocolManager::GetInstance().GetAvailableProtocols();
        std::vector<std::string> protocols;
        for (const auto& p : all_protocols) {
            if (p.find("OSC") == std::string::npos) {
                protocols.push_back(p);
            }
        }

        int currentIdx = -1;
        for(int i=0; i<protocols.size(); ++i) {
            if(protocols[i] == m_Impl->selectedProtocol) currentIdx = i;
        }

        if (ImGui::Combo("Format", &currentIdx, [](void* data, int idx, const char** out_text) {
            auto* protos = (std::vector<std::string>*)data;
            *out_text = (*protos)[idx].c_str();
            return true;
        }, &protocols, protocols.size())) {
            newProtocol = protocols[currentIdx];
        }
    }

    if (!newProtocol.empty()) {
        SetProtocol(newProtocol);
    }

    if (running) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Running (Port %d)", runningPort);
        if (runningPort != currentPort) {
            ImGui::SameLine();
            if (restartPending) {
                ImGui::TextDisabled("(Restarting...)");
            } else if (ImGui::Button("Restart to apply")) {
                Stop();
                std::lock_guard<std::mutex> lock(m_Impl->mutex);
                m_Impl->restartPending = true;
                m_Impl->restartPort = currentPort;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            Stop();
        ImGui::Separator();
        ImGui::Text("Connected Clients: %d", clientCount);

        if (ImGui::TreeNode("Client List")) {
            if (ImGui::BeginChild("Clients", ImVec2(0, 100), true)) {
                for (const auto &pair : clients) {
                    ImGui::TextUnformatted(pair.second.c_str());
                }
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Status: Stopped");
        ImGui::SameLine();
        if (ImGui::Button("Start"))
            Start(portInput);
    }

    ImGui::Separator();
    ImGui::Text("Log");
    if (ImGui::BeginChild("Log", ImVec2(0, 150), true)) {
        for (const auto &log : logs) {
            ImGui::TextUnformatted(log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void WebSocketServer::SetOutputMapper(OutputMapper* mapper) {
    m_OutputMapper = mapper;
}
#endif
