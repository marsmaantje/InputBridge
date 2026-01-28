#pragma once

#include <string>

#ifdef ENABLE_WEBSOCKETS
#include "App.h"
#else
// Define dummy OpCode if uWS is not available to avoid compilation errors in consumers
namespace uWS {
    enum OpCode {
        TEXT = 1,
        BINARY = 2
    };
}
#endif

class WebSocketServer {
public:
    static WebSocketServer& GetInstance();

    void Start(int port);
    void Stop();
    bool IsRunning() const;
    int GetPort() const;
    void SetPort(int port);
    int GetClientCount() const;

    void Broadcast(const std::string& msg, uWS::OpCode opCode);
    void DrawUI();

private:
    WebSocketServer();
    ~WebSocketServer();
    
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    struct Impl;
    Impl* m_Impl;
};