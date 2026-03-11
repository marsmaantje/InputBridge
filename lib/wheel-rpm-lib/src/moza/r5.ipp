// VID 0x346E  PID 0x0005
// The R5 kit ships with a round rim that has no LED strip.
// setRPM / setLEDs are intentional no-ops.

#include "wheel/wheel.hpp"

namespace wheel {

class MOZA_R5 : public Wheel {
public:
    bool setRPM(float percent) override { (void)percent; return false; }
    bool setLEDs(const std::vector<uint8_t>& leds) override { (void)leds; return false; }
    std::string name() const override { return "Moza R5"; }
};

} // namespace wheel
