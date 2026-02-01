#include "OSCServer.h"

#if ENABLE_OSC

#include "Protocols/ProtocolManager.h"
#include "Protocols/OSCProtocol.h"
#include "imgui.h"
#include <lo/lo.h>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

struct OSCServer::Impl {
    int port = 9000;
    bool running = false;
    int clientCount = 0;
    int runningPort = 0;
    bool restartPending = false;
    int restartPort = 0;
    std::mutex mutex;
    std::deque<std::string> logs;
    
    // Clients: URL string -> lo_address
    std::map<std::string, lo_address> clients;
    
    std::shared_ptr<IProtocol> protocol;
    std::string selectedProtocol;

    std::thread *thread = nullptr;
    lo_server server = nullptr;
};

OSCServer &OSCServer::GetInstance() {
    static OSCServer instance;
    return instance;
}

OSCServer::OSCServer() : m_Impl(new Impl) {
    m_Impl->protocol = ProtocolManager::GetInstance().GetProtocol("OSC");
    if (m_Impl->protocol) {
        m_Impl->selectedProtocol = m_Impl->protocol->getProtocolName();
    } else {
        m_Impl->selectedProtocol = "OSC (Built-in)";
    }
}

OSCServer::~OSCServer() {
    Stop();
    if (m_Impl->thread) {
        if (m_Impl->thread->joinable()) m_Impl->thread->join();
        delete m_Impl->thread;
    }
    for (auto& kv : m_Impl->clients) {
        lo_address_free(kv.second);
    }
    m_Impl->clients.clear();
    delete m_Impl;
}

static int osc_handler(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data) {
    // We need to access the Impl members. Since this is a static helper, 
    // we can cast it to the struct pointer because it's defined in this translation unit.
    auto* serverImpl = static_cast<OSCServer::Impl*>(user_data);
    std::lock_guard<std::mutex> lock(serverImpl->mutex);
    
    // Get source address
    lo_address src = lo_message_get_source(msg);
    if (src) {
        char *url = lo_address_get_url(src);
        if (url) {
            std::string urlStr(url);
            if (serverImpl->clients.find(urlStr) == serverImpl->clients.end()) {
                // New client
                lo_address newAddr = lo_address_new_from_url(url);
                if (newAddr) {
                    serverImpl->clients[urlStr] = newAddr;
                    serverImpl->clientCount++;
                    serverImpl->logs.push_back("Client connected: " + urlStr);
                    if (serverImpl->logs.size() > 100) serverImpl->logs.pop_front();
                }
            }
            free(url);
        }
    }

    // Log message
    std::string logMsg = "OSC Recv: " + std::string(path) + " " + (types ? types : "");
    serverImpl->logs.push_back(logMsg);
    if (serverImpl->logs.size() > 100) serverImpl->logs.pop_front();
    
    return 0;
}

static void osc_error(int num, const char *msg, const char *path) {
    // Handle error
}

void OSCServer::Start(int port) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    if (m_Impl->running)
        return;

    m_Impl->port = port;
    m_Impl->running = true;
    m_Impl->runningPort = port;

    if (m_Impl->thread) {
        if (m_Impl->thread->joinable()) m_Impl->thread->join();
        delete m_Impl->thread;
    }

    m_Impl->thread = new std::thread([this, port]() {
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", port);
        
        lo_server server = lo_server_new(portStr, osc_error);
        
        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            if (!server) {
                m_Impl->logs.push_back("Failed to start OSC server on port " + std::to_string(port));
                m_Impl->running = false;
                return;
            }
            m_Impl->server = server;
            m_Impl->logs.push_back("OSC server listening on port " + std::to_string(port));
            m_Impl->clientCount = 0;
            for(auto& kv : m_Impl->clients) {
                lo_address_free(kv.second);
            }
            m_Impl->clients.clear();
        }

        lo_server_add_method(server, NULL, NULL, osc_handler, m_Impl);

        while (true) {
            bool running;
            {
                std::lock_guard<std::mutex> lock(m_Impl->mutex);
                running = m_Impl->running;
            }
            if (!running) break;

            lo_server_recv_noblock(server, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            lo_server_free(m_Impl->server);
            m_Impl->server = nullptr;
            m_Impl->running = false;
            m_Impl->runningPort = 0;
        }
    });
}

void OSCServer::Stop() {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->running = false;
    m_Impl->restartPending = false;
}

bool OSCServer::IsRunning() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->running;
}

int OSCServer::GetPort() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->port;
}

void OSCServer::SetPort(int port) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->port = port;
}

int OSCServer::GetClientCount() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->clientCount;
}

void OSCServer::Broadcast(const std::string &address, float value) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    for (auto const &[url, addr] : m_Impl->clients) {
        lo_send(addr, address.c_str(), "f", value);
    }
}

void OSCServer::Broadcast(const std::string &address, int value) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    for (auto const &[url, addr] : m_Impl->clients) {
        lo_send(addr, address.c_str(), "i", value);
    }
}

void OSCServer::Broadcast(const std::string &address, const std::string &value) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    for (auto const &[url, addr] : m_Impl->clients) {
        lo_send(addr, address.c_str(), "s", value.c_str());
    }
}

void OSCServer::Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    for (auto const &[url, addr] : m_Impl->clients) {
        lo_send(addr, "/wheel", "fffff", wheel, brake, throttle, pitch, roll);
    }
}

void OSCServer::DrawContent() {
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
        std::map<std::string, lo_address> clients;
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

        // OSC Format selection
        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            auto oscProtocol = std::dynamic_pointer_cast<OSCProtocol>(m_Impl->protocol);
            if (oscProtocol) {
                int currentFormat = (int)oscProtocol->getProtocolVersion();
                if (ImGui::Combo(
                        "Format", &currentFormat,
                        [](void *, int idx, const char **out_text) {
                            *out_text = OSCProtocol::GetVersionLabel(idx);
                            return true;
                        },
                        nullptr, OSCProtocol::GetVersionCount())) {
                    oscProtocol->setProtocolVersion((OSCProtocol::ProtocolVersion)currentFormat);
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
                        ImGui::TextUnformatted(pair.first.c_str());
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