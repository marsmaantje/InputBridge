// VID 0x346E  PID 0x000A
// HGP (Hand-Grip Paddles) – paddle-only rim with no steering wheel face.
// This rim has no shift-indicator LEDs; setRPM / setLEDs are intentional no-ops.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "wheel/wheel.hpp"

namespace wheel {

class MOZA_HGP : public Wheel {
public:
    bool setRPM(float percent) override { (void)percent; return false; }
    bool setLEDs(const std::vector<uint8_t>& leds) override { (void)leds; return false; }
    std::string name() const override { return "Moza HGP"; }
};

} // namespace wheel
