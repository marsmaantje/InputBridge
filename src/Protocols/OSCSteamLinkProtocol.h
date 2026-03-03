#pragma once
#include "Protocols/OSCBaseProtocol.h"
#include "Protocols/ProtocolDefinition.h"
#include <map>
#include <string>

class OSCSteamLinkProtocol : public OSCBaseProtocol {
public:
    std::string getProtocolName() const override;
    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address, const std::string &value) override;
    std::string format_wheel(const std::map<std::string, float>& values) override;
    static ProtocolDefinition CreateDefaultDefinition();
};
