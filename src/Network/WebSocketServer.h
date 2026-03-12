#pragma once

#include <string>
#include <atomic>

#if ENABLE_WEBSOCKETS
#include <App.h>
#endif

class IProtocol;
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

    void DrawContent();

    void LoadConfig(const PreferencesManager& prefs);
    void SaveConfig(PreferencesManager& prefs);

    void SetOutputMapper(OutputMapper* mapper);
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

    void DrawContent() {}

    void LoadConfig(const PreferencesManager& prefs) {}
    void SaveConfig(PreferencesManager& prefs) {}

    void SetOutputMapper(OutputMapper* mapper) {}
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