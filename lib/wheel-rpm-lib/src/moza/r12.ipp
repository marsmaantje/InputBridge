// VID 0x346E  PID 0x000B
// 10 shift-indicator LEDs, same physical layout as R9.

#include "moza_base.hpp"
#include "wheel/utils/rpm_mapper.hpp"

namespace wheel {

class MOZA_R12 : public MozaBase {
public:
    static constexpr int kLEDs = 10;

    MOZA_R12(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}

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

    std::string name() const override { return "Moza R12"; }
};

} // namespace wheel
