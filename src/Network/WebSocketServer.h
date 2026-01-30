#pragma once

#include <string>

#ifdef ENABLE_WEBSOCKETS
#include <App.h>
#endif

class IProtocol;

class WebSocketServer {
  public:
    static WebSocketServer &GetInstance();

    void Start(int port);
    void Stop();
    bool IsRunning() const;
    int GetPort() const;
    void SetPort(int port);
    int GetClientCount() const;

#ifdef ENABLE_WEBSOCKETS
    void Broadcast(const std::string &address, float value);
    void Broadcast(const std::string &address, int value);
    void Broadcast(const std::string &address, const std::string &value);
    void Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll);
    void Broadcast(const std::string &msg, uWS::OpCode opCode);
#else
    // Provide a dummy implementation or an alternative signature when
    // websockets are disabled
    void Broadcast(const std::string &address, float value) {}
    void Broadcast(const std::string &address, int value) {}
    void Broadcast(const std::string &address, const std::string &value) {}
    void Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll) {}
    void Broadcast(const std::string &msg, int opCode = 0) {}
#endif

    void DrawUI();

  private:
    WebSocketServer();
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer &) = delete;
    WebSocketServer &operator=(const WebSocketServer &) = delete;

    struct Impl;
    Impl *m_Impl;
};