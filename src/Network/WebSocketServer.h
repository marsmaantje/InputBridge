#pragma once

#include <string>
#include <atomic>

#if ENABLE_WEBSOCKETS
#include <App.h>
#endif

class IProtocol;

/**
 * @brief WebSocket transport: sends input data as JSON and receives haptic commands.
 *
 * WebSocketServer satisfies the ITransport contract (see Network/ITransport.h).
 * Incoming haptic JSON messages are forwarded to HapticParser::Parse(), which
 * then calls OutputMapper::Queue*() — no parsing logic lives here.
 *
 * @see ITransport       Abstract transport interface documenting the full contract.
 * @see OSCServer        Parallel implementation for OSC/UDP.
 * @see HapticParser     JSON haptic message parser used by this server.
 * @see HapticDispatcher OSC argument parser (used by OSCServer, not this class).
 */
class PreferencesManager;
class OutputMapper;

class WebSocketServer {
  public:
#if ENABLE_WEBSOCKETS
    static WebSocketServer &GetInstance();

    void Start(int port);
    void Stop();
    // Block until the uWS event-loop thread has fully exited.  Must be called
    // after Stop() and before destroying any objects the thread's callbacks
    // reference (e.g. OutputMapper).
    void WaitStopped();
    bool IsRunning() const;
    int GetPort() const;
    void SetPort(int port);
    int GetClientCount() const;

    void SetSelectedDevice(int id);
    int GetSelectedDevice() const;

    void SetProtocol(const std::string& name);
    std::string GetProtocol() const;

    /** Select a user-defined WebSocket protocol from ProtocolRegistry by its
     *  definition ID.  Passing an empty string reverts to the built-in selection. */
    void SetOutputDefinition(const std::string& definitionId);
    void SetInputDefinition(const std::string& definitionId);
    std::string GetOutputDefinitionId() const;
    std::string GetInputDefinitionId() const;

    void SetDefinition(const std::string& definitionId);
    std::string GetDefinitionId() const;

    void Broadcast(const std::string &address, float value);
    void Broadcast(const std::string &address, int value);
    void Broadcast(const std::string &address, const std::string &value);
    void Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll);
    void Broadcast(const std::string &msg, uWS::OpCode opCode);

    // Sends battery level and charging state for one device over WebSocket.
    // Mirrors OSCServer::SendBattery — see that method for parameter semantics.
    void BroadcastBattery(int deviceIndex, int battery_percent, bool charging,
                          int battery_percent_L = -1);

    void DrawContent();

    void LoadConfig(const PreferencesManager& prefs);
    void SaveConfig(PreferencesManager& prefs);

    void SetOutputMapper(OutputMapper* mapper);

    // Call once per frame from the main loop — fires StopAllHapticEffects when
    // connected clients go silent for longer than the inactivity timeout.
    // Write port field from a profile without restarting the server.
    void SetPortFromProfile(int port);

    void CheckInactivity();

    bool IsOutputEnabled() const;
    bool IsInputEnabled()  const;
    void SetOutputEnabled(bool enabled);
    void SetInputEnabled(bool enabled);

    void SetInactivityTimeoutEnabled(bool enabled);
    void SetInactivityTimeoutMs(uint64_t ms);
#else
    static WebSocketServer &GetInstance() {
        static WebSocketServer i;
        return i;
    }

    void Start(int port) {}
    void Stop() {}
    void WaitStopped() {}
    bool IsRunning() const { return false; }
    int GetPort() const { return 0; }
    void SetPort(int port) {}
    int GetClientCount() const { return 0; }

    void SetSelectedDevice(int id) {}
    int GetSelectedDevice() const { return 0; }

    void SetProtocol(const std::string& name) {}
    std::string GetProtocol() const { return ""; }

    void SetOutputDefinition(const std::string& definitionId) {}
    void SetInputDefinition(const std::string& definitionId) {}
    std::string GetOutputDefinitionId() const { return ""; }
    std::string GetInputDefinitionId() const { return ""; }
    void SetDefinition(const std::string& definitionId) {}
    std::string GetDefinitionId() const { return ""; }

    void Broadcast(const std::string &address, float value) {}
    void Broadcast(const std::string &address, int value) {}
    void Broadcast(const std::string &address, const std::string &value) {}
    void Broadcast_wheel(float wheel, float brake, float throttle, float pitch, float roll) {}
    void BroadcastBattery(int deviceIndex, int battery_percent, bool charging,
                          int battery_percent_L = -1) {}

    void DrawContent() {}

    void LoadConfig(const PreferencesManager& prefs) {}
    void SaveConfig(PreferencesManager& prefs) {}

    void SetOutputMapper(OutputMapper* mapper) {}

    void SetPortFromProfile(int port) {}

    void CheckInactivity() {}

    bool IsOutputEnabled() const { return true; }
    bool IsInputEnabled()  const { return true; }
    void SetOutputEnabled(bool) {}
    void SetInputEnabled(bool)  {}
    void SetInactivityTimeoutEnabled(bool) {}
    void SetInactivityTimeoutMs(uint64_t) {}
#endif

  private:
#if ENABLE_WEBSOCKETS
    WebSocketServer();
    ~WebSocketServer();
#else
    WebSocketServer() : m_selectedDeviceId(0), m_Impl(nullptr) {}
    ~WebSocketServer() {}
#endif

    WebSocketServer(const WebSocketServer &) = delete;
    WebSocketServer &operator=(const WebSocketServer &) = delete;

    std::atomic<int> m_selectedDeviceId{0};
    struct Impl;
    Impl *m_Impl;
    OutputMapper* m_OutputMapper = nullptr;
};