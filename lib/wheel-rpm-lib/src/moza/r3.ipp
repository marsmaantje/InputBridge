// VID 0x346E  PID 0x0001
// The R3 kit ships with a compact round rim that has no LED strip.
// setRPM / setLEDs are intentional no-ops.

#include "wheel/wheel.hpp"

namespace wheel {

class MOZA_R3 : public Wheel {
public:
    bool setRPM(float percent) override { (void)percent; return false; }
    bool setLEDs(const std::vector<uint8_t>& leds) override { (void)leds; return false; }
    std::string name() const override { return "Moza R3"; }
};

} // namespace wheel
