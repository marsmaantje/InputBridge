#pragma once
#include "Protocols/OSCBaseProtocol.h"
#include "Protocols/ProtocolDefinition.h"

class OSCSteamLinkProtocol : public OSCBaseProtocol {
public:
    std::string getProtocolName() const override;
    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address, const std::string &value) override;
    std::string format_wheel(float wheel, float brake, float throttle, float pitch, float roll) override;
    static ProtocolDefinition CreateDefaultDefinition();
};
