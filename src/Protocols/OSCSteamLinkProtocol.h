#pragma once
#include "Protocols/OSCBaseProtocol.h"

class OSCSteamLinkProtocol : public OSCBaseProtocol {
public:
    std::string getProtocolName() const override;
};
