
#pragma once
#include "wheel/wheel.hpp"
#include "wheel/transport.hpp"
#include <memory>

namespace wheel {

class LogitechWheel : public Wheel {

protected:

    std::unique_ptr<HIDTransport> hid;

    bool sendMask(uint8_t mask)
    {
        uint8_t report[4] = {0xF8,0x00,0x00,mask};
        return hid->write(report,4);
    }

public:

    LogitechWheel(std::unique_ptr<HIDTransport> t)
        : hid(std::move(t)) {}

};

}
