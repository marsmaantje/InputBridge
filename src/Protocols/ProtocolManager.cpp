#include "ProtocolManager.h"
#include <map>
#include "Protocols/MarsmaantjeOldProtocol.h"
#include "Protocols/MarsmaantjeNewProtocol.h"
#include "Protocols/OSCDefaultProtocol.h"
#include "Protocols/OSCSteamLinkProtocol.h"
#include "Protocols/OSCProjectBabbleProtocol.h"

struct ProtocolManager::Impl {
    std::map<std::string, std::shared_ptr<IProtocol>> protocols;
};

ProtocolManager &ProtocolManager::GetInstance() {
    static ProtocolManager instance;
    return instance;
}

ProtocolManager::ProtocolManager() : m_Impl(new Impl) {
    // Register default protocols
    RegisterProtocol(std::make_shared<MarsmaantjeOldProtocol>());
    RegisterProtocol(std::make_shared<MarsmaantjeNewProtocol>());
    RegisterProtocol(std::make_shared<OSCDefaultProtocol>());
    RegisterProtocol(std::make_shared<OSCSteamLinkProtocol>());
    RegisterProtocol(std::make_shared<OSCProjectBabbleProtocol>());
}

ProtocolManager::~ProtocolManager() { delete m_Impl; }

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
