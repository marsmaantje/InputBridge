#pragma once

#include <string>

#if ENABLE_WEBSOCKETS
#include <App.h>
#endif

class IProtocol;

class WebSocketServer {
  public:
#if ENABLE_WEBSOCKETS
    static WebSocketServer &GetInstance();

    void Start(int port);
    void Stop();
    bool IsRunning() const;
    int GetPort() const;
    void SetPort(int port);
    int GetClientCount() const;

    void Broadcast(const std::string &address, float value);
    void Broadcast(const std::string &address, int value);
    void Broadcast(const std::string &address, const std::string &value);
    void Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll);
    void Broadcast(const std::string &msg, uWS::OpCode opCode);

    void DrawContent();
#else
    static WebSocketServer &GetInstance() {
        static WebSocketServer i;
        return i;
    }

    void Start(int port) {}
    void Stop() {}
    bool IsRunning() const { return false; }
    int GetPort() const { return 0; }
    void SetPort(int port) {}
    int GetClientCount() const { return 0; }

    void Broadcast(const std::string &address, float value) {}
    void Broadcast(const std::string &address, int value) {}
    void Broadcast(const std::string &address, const std::string &value) {}
    void Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll) {}

    void DrawContent() {}
#endif

  private:
#if ENABLE_WEBSOCKETS
    WebSocketServer();
    ~WebSocketServer();
#else
    WebSocketServer() : m_Impl(nullptr) {}
    ~WebSocketServer() {}
#endif

    WebSocketServer(const WebSocketServer &) = delete;
    WebSocketServer &operator=(const WebSocketServer &) = delete;

    struct Impl;
    Impl *m_Impl;
};