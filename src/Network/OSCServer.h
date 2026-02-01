#pragma once

#include <string>

class OSCServer {
public:
    static OSCServer& GetInstance();
    struct Impl;

    OSCServer();
    ~OSCServer();

    void Start(int port);
    void Stop();
    bool IsRunning() const;
    int GetPort() const;
    void SetPort(int port);
    int GetClientCount() const;

    void Broadcast(const std::string& address, float value);
    void Broadcast(const std::string& address, int value);
    void Broadcast(const std::string& address, const std::string& value);
    void Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll);

    void DrawContent();

private:
    Impl* m_Impl;
};
