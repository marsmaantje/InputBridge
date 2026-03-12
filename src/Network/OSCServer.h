#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <lo/lo.h>
#include <mutex>
#include <deque>
#include <set>
#include <memory>
#include <vector>
#include <thread>

class PreferencesManager;
class OutputMapper;
class IProtocol;
class OSCBaseProtocol;

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
    // Block until the liblo cleanup thread (spawned by Stop()) has fully
    // exited.  Must be called after Stop() and before destroying any object
    // the liblo callbacks reference (e.g. OutputMapper).
    void WaitStopped();

    bool IsRunning() const;

    // Returns true after the OSCServer singleton has been fully destroyed.
    // Use in destructors of objects that may outlive the server (e.g. OSCProtocol)
    // to guard against use-after-destruction when calling GetInstance().
    static bool IsDestroyed();

    void Send(const std::string& path, const char* types, ...);
    void SendWheel(float steer, float brake, float throttle, float pitch, float roll);
    void SendButtons(const std::vector<uint32_t>& buttons);

    void SetSelectedDevice(int id);
    int GetSelectedDevice() const;

    const char* GetSendHost() const;
    int GetSendPort() const;
    int GetReceivePort() const;

    void SetHandler(OSCHandler handler);

    void DrawContent();

    void SetProtocol(const std::string& name);
    std::string GetProtocol() const;

    /** Select a user-defined protocol from ProtocolRegistry by its definition ID.
     *  Passing an empty string clears the definition selection (falls back to
     *  the built-in protocol set via SetProtocol). */
    /** Select user-defined output (server→client) and input (client→server)
     *  protocol definitions independently.  Pass "" to clear a selection. */
    void SetOutputDefinition(const std::string& definitionId);
    void SetInputDefinition(const std::string& definitionId);
    std::string GetOutputDefinitionId() const;
    std::string GetInputDefinitionId() const;

    // Legacy kept for compatibility
    void SetDefinition(const std::string& definitionId);
    std::string GetDefinitionId() const;

    void LoadConfig(const PreferencesManager& prefs);
    void SaveConfig(PreferencesManager& prefs);

    void SetOutputMapper(OutputMapper* mapper);

private:
    static int generic_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_rumble_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_constant_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_periodic_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_condition_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_gain_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);

    lo_address m_send_address = nullptr;
    lo_server_thread m_server_thread = nullptr;

    std::atomic<bool> m_running = false;
    std::atomic<bool> m_isConnected = false;
    std::atomic<int> m_selectedDeviceId = 0;

    // UI State
    char m_send_host[128] = "127.0.0.1";
    int m_send_port = 9066;
    int m_recv_port = 9068;

    // Running State
    std::string m_running_send_host;
    int m_running_send_port = 0;
    int m_running_recv_port = 0;

    OSCHandler m_handler;
    mutable std::mutex m_mutex;  // mutable for const methods
    std::shared_ptr<IProtocol> m_protocol;
    std::string m_protocolName = "OSC Back Ally Racing";
    std::string m_selectedDefinitionId; // legacy single-slot
    std::string m_outputDefinitionId; // selected output (server→client) definition
    std::string m_inputDefinitionId;  // selected input  (client→server) definition
    std::deque<std::string> m_logs;
    std::set<std::string> m_clients;
    uint64_t m_lastMessageTime = 0;
    OutputMapper* m_OutputMapper = nullptr;

    // Cleanup thread used by Stop() to run lo_server_thread_stop off the
    // main/UI thread.  Stored (not detached) so the destructor can join it
    // and guarantee the thread finishes before members are destroyed.
    std::thread m_cleanupThread;
};