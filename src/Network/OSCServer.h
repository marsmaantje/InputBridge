#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <lo/lo.h>
#include <mutex>
#include <deque>
#include <set>

// Callback for incoming OSC messages
using OSCHandler = std::function<void(const char* path, const char* types, lo_arg** argv, int argc)>;

class OSCServer {
public:
    static OSCServer& GetInstance();

    enum class ProtocolVersion {
        Default,
        WaterSteeringWheelPy,
        MarsmaantjeNew
    };

    OSCServer();
    ~OSCServer();

    // Starts both sending and receiving
    bool Start(const std::string& send_host, int send_port, int recv_port);
    void Stop();

    bool IsRunning() const;

    void Send(const std::string& path, const char* types, ...);
    void SendWheel(float steer, float brake, float throttle, float pitch, float roll);
    void SendButtons(int buttons);

    void SetHandler(OSCHandler handler);
    
    void DrawContent();

    void SetProtocolVersion(ProtocolVersion version);
    ProtocolVersion GetProtocolVersion() const;

private:
    static int generic_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);

    lo_address m_send_address = nullptr;
    lo_server_thread m_server_thread = nullptr;

    std::atomic<bool> m_running = false;
    
    // UI State
    char m_send_host[128] = "127.0.0.1";
    int m_send_port = 9066;
    int m_recv_port = 9068;

    OSCHandler m_handler;
    std::mutex m_mutex;
    ProtocolVersion m_protocolVersion = ProtocolVersion::Default;
    std::deque<std::string> m_logs;
    std::set<std::string> m_clients;
};
