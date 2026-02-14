#pragma once
#include "Protocols/OSCBaseProtocol.h"

class OSCDefaultProtocol : public OSCBaseProtocol {
public:
    std::string getProtocolName() const override { return "OSC Default"; }
};
