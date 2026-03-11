// VID 0x346E  PID 0x0007
// ES (Endurance Sport) Steering Wheel – oval endurance-style rim with 10-LED strip.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "moza_base.hpp"
#include "wheel/utils/rpm_mapper.hpp"

namespace wheel {

class MOZA_ES : public MozaBase {
public:
    static constexpr int kLEDs = 10;

    MOZA_ES(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}

    bool setRPM(float percent) override
    {
        auto leds = RPMMapper::linear(percent, kLEDs);
        uint8_t values[kLEDs]{};
        for (int i = 0; i < kLEDs; ++i)
            values[i] = leds[i] ? 0xFF : 0x00;
        return sendLEDs(values, kLEDs);
    }

    bool setLEDs(const std::vector<uint8_t>& leds) override
    {
        uint8_t values[kLEDs]{};
        for (int i = 0; i < kLEDs; ++i)
            values[i] = (i < static_cast<int>(leds.size())) ? leds[i] : 0;
        return sendLEDs(values, kLEDs);
    }

    std::string name() const override { return "Moza ES"; }
};

} // namespace wheel
