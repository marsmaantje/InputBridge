#include "WebSocketServer.h"
#include "imgui.h"
#include <thread>
#include <mutex>
#include <iostream>

#ifdef ENABLE_WEBSOCKETS
struct us_listen_socket_t;
#endif

struct WebSocketServer::Impl {
    int port = 9001;
    bool running = false;
    int clientCount = 0;
    std::mutex mutex;
    
#ifdef ENABLE_WEBSOCKETS
    std::thread* thread = nullptr;
    uWS::App* app = nullptr;
    uWS::Loop* loop = nullptr;
    struct us_listen_socket_t* listen_socket = nullptr;
#endif
};

WebSocketServer& WebSocketServer::GetInstance() {
    static WebSocketServer instance;
    return instance;
}

WebSocketServer::WebSocketServer() : m_Impl(new Impl) {
}

WebSocketServer::~WebSocketServer() {
    Stop();
    delete m_Impl;
}

void WebSocketServer::Start(int port) {
#ifdef ENABLE_WEBSOCKETS
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    if (m_Impl->running) return;

    m_Impl->port = port;
    m_Impl->running = true;

    if (m_Impl->thread) {
        delete m_Impl->thread;
    }

    m_Impl->thread = new std::thread([this, port]() {
        uWS::App app;
        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            m_Impl->app = &app;
            m_Impl->loop = uWS::Loop::get();
            m_Impl->clientCount = 0;
        }
        
        app.ws<int>("/*", {
            .open = [this](auto *ws) { 
                ws->subscribe("broadcast");
                std::lock_guard<std::mutex> lock(m_Impl->mutex);
                m_Impl->clientCount++;
            },
            .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
                // std::cout << "FFB Data: " << message << std::endl;
            },
            .close = [this](auto *ws, int code, std::string_view message) {
                std::lock_guard<std::mutex> lock(m_Impl->mutex);
                m_Impl->clientCount--;
            }
        }).listen(port, [this](auto *listen_socket) {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            if (listen_socket) {
                std::cout << "WebSocket server listening on port " << m_Impl->port << std::endl;
                m_Impl->listen_socket = (struct us_listen_socket_t*)listen_socket;
            } else {
                std::cout << "Failed to listen on port " << m_Impl->port << std::endl;
                m_Impl->running = false;
            }
        }).run();

        {
            std::lock_guard<std::mutex> lock(m_Impl->mutex);
            m_Impl->app = nullptr;
            m_Impl->loop = nullptr;
            m_Impl->listen_socket = nullptr;
            m_Impl->running = false;
        }
    });
    m_Impl->thread->detach();
#else
    std::cout << "WebSocket server disabled. Define ENABLE_WEBSOCKETS to enable." << std::endl;
#endif
}

void WebSocketServer::Stop() {
#ifdef ENABLE_WEBSOCKETS
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    if (m_Impl->loop && m_Impl->listen_socket) {
        struct us_listen_socket_t* socket = m_Impl->listen_socket;
        m_Impl->loop->defer([socket]() {
            us_listen_socket_close(0, socket);
        });
    }
#endif
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

void WebSocketServer::Broadcast(const std::string& msg, uWS::OpCode opCode) {
#ifdef ENABLE_WEBSOCKETS
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    if (m_Impl->loop && m_Impl->app) {
        m_Impl->loop->defer([this, msg, opCode]() {
            if (m_Impl->app) m_Impl->app->publish("broadcast", msg, opCode);
        });
    }
#endif
}

void WebSocketServer::DrawUI() {
    if (ImGui::Begin("WebSocket Server")) {
#ifdef ENABLE_WEBSOCKETS
        
        bool running = IsRunning();
        int currentPort = GetPort();
        int clientCount = GetClientCount();

        if (running) {
            ImGui::BeginDisabled();
        }
        
        int portInput = currentPort;
        if (ImGui::InputInt("Port", &portInput)) {
            SetPort(portInput);
        }
        
        if (running) ImGui::EndDisabled();

        if (running) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Running");
            ImGui::SameLine();
            if (ImGui::Button("Stop")) Stop();
            ImGui::Text("Connected Clients: %d", clientCount);
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Status: Stopped");
            ImGui::SameLine();
            if (ImGui::Button("Start")) Start(portInput);
        }
#else
        ImGui::TextDisabled("WebSocket support not compiled (ENABLE_WEBSOCKETS missing)");
#endif
    }
    ImGui::End();
}