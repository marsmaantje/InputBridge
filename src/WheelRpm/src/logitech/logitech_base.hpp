
#pragma once
#include "wheel/wheel.hpp"
#include "wheel/transport.hpp"
#include "wheel/utils/rpm_mapper.hpp"
#include <memory>

namespace wheel {

class LogitechWheel : public Wheel {

protected:

    std::unique_ptr<HIDTransport> hid;

    static constexpr int kLEDs = 5;

    bool sendMask(uint8_t mask)
    {
        uint8_t report[4] = {0xF8,0x00,0x00,mask};
        return hid->write(report,4);
    }

public:

    LogitechWheel(std::unique_ptr<HIDTransport> t)
        : hid(std::move(t)) {}

    bool setRPM(float percent) override
    {
        auto leds = RPMMapper::linear(percent, kLEDs);

        uint8_t mask = 0;

        for(size_t i=0; i<leds.size(); i++)
            if(leds[i])
                mask |= (1<<i);

        return sendMask(mask);
    }

    bool setLEDs(const std::vector<uint8_t>& leds) override
    {
        uint8_t mask = 0;

        for(size_t i=0; i<leds.size() && i<kLEDs; i++)
            if(leds[i])
                mask |= (1<<i);

        return sendMask(mask);
    }
};

}
