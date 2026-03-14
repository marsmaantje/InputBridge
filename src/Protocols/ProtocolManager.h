#pragma once
#include "IProtocol.h"
#include <memory>
#include <string>
#include <vector>

class ProtocolManager {
  public:
    static ProtocolManager &GetInstance();

    void RegisterProtocol(std::shared_ptr<IProtocol> protocol);
    std::shared_ptr<IProtocol> GetProtocol(const std::string &name) const;
    std::vector<std::string> GetAvailableProtocols() const;

    // Release all registered protocol instances explicitly.  Call this during
    // Application::Shutdown() — before any of the singletons that protocols
    // reference (OSCServer, DeviceManager, …) are torn down.  Without this
    // the ProtocolManager static singleton outlives OSCServer (because it was
    // constructed first) and protocol destructors touch dead objects.
    void Clear();

    void SetActiveInputProtocolId(const std::string& id);
    std::string GetActiveInputProtocolId() const;

    void SetActiveInputLegacyProtocol(const std::string& name);
    std::string GetActiveInputLegacyProtocol() const;

  private:
    ProtocolManager();
    ~ProtocolManager();

    ProtocolManager(const ProtocolManager &) = delete;
    ProtocolManager &operator=(const ProtocolManager &) = delete;

    struct Impl;
    Impl *m_Impl;
};