
#pragma once
#include "wheel/wheel.hpp"
#include "wheel/transport.hpp"
#include "wheel/utils/rpm_mapper.hpp"
#include <memory>

namespace wheel {

class FanatecWheel : public Wheel {

protected:

    std::unique_ptr<HIDTransport> hid;

    static constexpr int kLEDs = 9;
    static constexpr size_t kReportSize = 10;

public:

    FanatecWheel(std::unique_ptr<HIDTransport> t)
        : hid(std::move(t)) {}

    bool setRPM(float percent) override
    {
        uint8_t report[16]{};

        report[0] = 0x08;

        auto leds = RPMMapper::linear(percent, kLEDs);

        for(size_t i=0; i<leds.size(); i++)
            report[i+1] = leds[i] ? 0xFF : 0;

        return hid->write(report, kReportSize);
    }

    bool setLEDs(const std::vector<uint8_t>& leds) override
    {
        uint8_t report[16]{};

        report[0] = 0x08;

        for(size_t i=0; i<leds.size() && i<kLEDs; i++)
            report[i+1] = leds[i];

        return hid->write(report, kReportSize);
    }
};

}
