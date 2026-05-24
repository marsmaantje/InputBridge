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

/**
 * @brief OSC transport: sends input data over UDP and receives haptic commands.
 *
 * OSCServer satisfies the ITransport contract (see Network/ITransport.h).
 * It is kept as a singleton rather than inheriting ITransport directly so that
 * existing call-sites (which obtain it via GetInstance()) need no changes.
 * A future refactor can make it inherit ITransport once the singleton is
 * replaced with dependency injection at the Application level.
 *
 * @see ITransport      Abstract transport interface documenting the full contract.
 * @see WebSocketServer Parallel implementation for JSON-over-WebSocket.
 * @see HapticDispatcher Centralised argument-parsing used by all haptic handlers.
 */

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
    bool HasClients() const;

    // Call once per frame from the main loop. Handles client inactivity
    // timeout and fires StopAllHapticEffects on the transition.
    void CheckInactivity();

    // Returns true after the OSCServer singleton has been fully destroyed.
    // Use in destructors of objects that may outlive the server (e.g. OSCProtocol)
    // to guard against use-after-destruction when calling GetInstance().
    static bool IsDestroyed();

    void Send(const std::string& path, const char* types, ...);
    void Send(const std::string& address, float value);
    void Send(const std::string& address, int value);
    void Send(const std::string& address, const std::string& value);
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

    bool IsOutputEnabled() const;
    bool IsInputEnabled()  const;
    void SetOutputEnabled(bool enabled);
    void SetInputEnabled(bool enabled);

    // Write port/host fields from a profile without restarting the server.
    // The caller must restart manually (or the UI shows "Restart to apply").
    void SetPortsFromProfile(const std::string& sendHost, int sendPort, int recvPort);

private:
    static int generic_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_rumble_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_constant_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_periodic_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_condition_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);
    static int haptic_gain_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);

    // Handles subchannel paths of the form /haptic/<effect>/<slot>
    // where the slot is encoded in the path instead of as a message argument.
    // This allows multiple effects of the same type to be sent in the same
    // frame (required by hosts like Resonite that allow only one message per
    // OSC path per frame).
    static int haptic_subchannel_handler(const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data);

    lo_address m_send_address = nullptr;
    lo_server_thread m_server_thread = nullptr;

    std::atomic<bool> m_running = false;
    std::atomic<bool> m_isConnected = false;
    std::atomic<int> m_selectedDeviceId = 0;

    // UI State
    char m_send_host[128];
    int m_send_port = 9066;
    int m_recv_port = 9068;

    // Running State
    std::string m_running_send_host;
    int m_running_send_port = 0;
    int m_running_recv_port = 0;

    OSCHandler m_handler;
    mutable std::mutex m_mutex;  // mutable for const methods
    std::shared_ptr<IProtocol> m_protocol;
    std::string m_protocolName;
    std::string m_selectedDefinitionId; // legacy single-slot
    std::string m_outputDefinitionId; // selected output (server→client) definition
    std::string m_inputDefinitionId;  // selected input  (client→server) definition
    struct LogEntry { std::string text; bool isError = false; };
    std::deque<LogEntry> m_logs;
    std::set<std::string> m_clients;
    uint64_t m_lastMessageTime = 0;
    OutputMapper* m_OutputMapper = nullptr;
    // Preserved across Stop()/Start() cycles so that restarting the server
    // from the UI does not silently disable all haptic effects.
    OutputMapper* m_savedOutputMapper = nullptr;

    // Direction enable flags — persisted to prefs.
    bool m_outputEnabled = true;  // send OSC messages to clients
    bool m_inputEnabled  = true;  // receive OSC messages from clients

    // Inactivity timeout — persisted to prefs.
    bool     m_inactivityTimeoutEnabled = true;
    uint64_t m_inactivityTimeoutMs      = 5000;

    // Cleanup thread used by Stop() to run lo_server_thread_stop off the
    // main/UI thread.  Stored (not detached) so the destructor can join it
    // and guarantee the thread finishes before members are destroyed.
    std::thread m_cleanupThread;
};
