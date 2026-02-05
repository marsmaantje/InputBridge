#include "WebSocketServer.h"
#include "Preferences/Preferences.h"

#if ENABLE_WEBSOCKETS

#include "Protocols/ProtocolManager.h"
#include "Protocols/WebSocketProtocol.h"
#include "imgui.h"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

struct us_listen_socket_t;

struct WebSocketServer::Impl {
    int port = 9001;
    bool running = false;
    int clientCount = 0;
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
    m_Impl->protocol = ProtocolManager::GetInstance().GetProtocol("WebSocket");
    if (!m_Impl->protocol) {
        m_Impl->protocol = std::make_shared<WebSocketProtocol>();
        ProtocolManager::GetInstance().RegisterProtocol(m_Impl->protocol);
    }
    m_Impl->selectedProtocol = m_Impl->protocol->getProtocolName();
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
            m_Impl->clientCount = 0;
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
                                   m_Impl->clientCount++;
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
                                   ProtocolManager::GetInstance().GetProtocol("WebSocket")->parse(std::string(message));
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
                                   m_Impl->clientCount--;
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
    return m_Impl->clientCount;
}

void WebSocketServer::SetSelectedDevice(int id) { m_selectedDeviceId = id; }

int WebSocketServer::GetSelectedDevice() const { return m_selectedDeviceId; }

void WebSocketServer::SetProtocolVersion(int version) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    auto wsProtocol = std::dynamic_pointer_cast<WebSocketProtocol>(m_Impl->protocol);
    if (wsProtocol) {
        wsProtocol->setProtocolVersion((WebSocketProtocol::ProtocolVersion)version);
    }
}

int WebSocketServer::GetProtocolVersion() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    auto wsProtocol = std::dynamic_pointer_cast<WebSocketProtocol>(m_Impl->protocol);
    if (wsProtocol) {
        return (int)wsProtocol->getProtocolVersion();
    }
    return 0;
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
    int port = prefs.GetInt("WebSocket", "Port", 9001);
    int protocol = prefs.GetInt("WebSocket", "Protocol", 0);
    bool enabled = prefs.GetBool("WebSocket", "Enabled", false);

    SetPort(port);
    SetProtocolVersion(protocol);

    if (enabled) {
        Start(port);
    }
}

void WebSocketServer::SaveConfig(PreferencesManager& prefs) {
    int port;
    int protocol = 0;
    bool running;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        port = m_Impl->port;
        running = m_Impl->running;
        auto wsProtocol = std::dynamic_pointer_cast<WebSocketProtocol>(m_Impl->protocol);
        if (wsProtocol) {
             protocol = (int)wsProtocol->getProtocolVersion();
        }
    }
    prefs.SetInt("WebSocket", "Port", port);
    prefs.SetInt("WebSocket", "Protocol", protocol);
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
        clientCount = m_Impl->clientCount;
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
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        auto wsProtocol = std::dynamic_pointer_cast<WebSocketProtocol>(m_Impl->protocol);
        if (wsProtocol) {
            int currentFormat = (int)wsProtocol->getProtocolVersion();
            if (ImGui::Combo(
                    "Format", &currentFormat,
                    [](void *, int idx, const char **out_text) {
                        *out_text = WebSocketProtocol::GetVersionLabel(idx);
                        return true;
                    },
                    nullptr, WebSocketProtocol::GetVersionCount())) {
                wsProtocol->setProtocolVersion((WebSocketProtocol::ProtocolVersion)currentFormat);
            }
        }
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
#endif
