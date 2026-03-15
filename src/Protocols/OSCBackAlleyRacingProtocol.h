#pragma once
#include "Protocols/OSCBaseProtocol.h"
#include <string>

class OSCBackAlleyRacingProtocol : public OSCBaseProtocol {
public:
    std::string getProtocolName() const override { return "OSC Back Ally Racing"; }
};
