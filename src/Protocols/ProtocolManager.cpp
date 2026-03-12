#include "ProtocolManager.h"
#include <map>
#include "Protocols/MarsmaantjeOldProtocol.h"
#include "Protocols/MarsmaantjeNewProtocol.h"
#include "Protocols/OSCBackAllyRacingProtocol.h"
#include "Protocols/OSCSteamLinkProtocol.h"
#include "Protocols/OSCProjectBabbleProtocol.h"

struct ProtocolManager::Impl {
    std::map<std::string, std::shared_ptr<IProtocol>> protocols;
    std::string activeInputProtocolId;
    std::string activeInputLegacyProtocol;
};

ProtocolManager &ProtocolManager::GetInstance() {
    static ProtocolManager instance;
    return instance;
}

ProtocolManager::ProtocolManager() : m_Impl(new Impl) {
    // Register default protocols
    RegisterProtocol(std::make_shared<MarsmaantjeOldProtocol>());
    RegisterProtocol(std::make_shared<MarsmaantjeNewProtocol>());
    RegisterProtocol(std::make_shared<OSCBackAllyRacingProtocol>());
    RegisterProtocol(std::make_shared<OSCSteamLinkProtocol>());
    RegisterProtocol(std::make_shared<OSCProjectBabbleProtocol>());
}

ProtocolManager::~ProtocolManager() { delete m_Impl; }

void ProtocolManager::Clear() {
    // Release all protocol shared_ptrs now, while the singletons they
    // reference (OSCServer, DeviceManager, etc.) are still alive.
    // The destructor will then delete an already-empty Impl, which is safe.
    m_Impl->protocols.clear();
}

void ProtocolManager::RegisterProtocol(std::shared_ptr<IProtocol> protocol) {
    if (protocol) {
        m_Impl->protocols[protocol->getProtocolName()] = protocol;
    }
}

std::shared_ptr<IProtocol>
ProtocolManager::GetProtocol(const std::string &name) const {
    auto it = m_Impl->protocols.find(name);
    if (it != m_Impl->protocols.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> ProtocolManager::GetAvailableProtocols() const {
    std::vector<std::string> names;
    for (const auto &pair : m_Impl->protocols) {
        names.push_back(pair.first);
    }
    return names;
}

void ProtocolManager::SetActiveInputProtocolId(const std::string& id) {
    m_Impl->activeInputProtocolId = id;
}

std::string ProtocolManager::GetActiveInputProtocolId() const {
    return m_Impl->activeInputProtocolId;
}

void ProtocolManager::SetActiveInputLegacyProtocol(const std::string& name) {
    m_Impl->activeInputLegacyProtocol = name;
}

std::string ProtocolManager::GetActiveInputLegacyProtocol() const {
    return m_Impl->activeInputLegacyProtocol;
}