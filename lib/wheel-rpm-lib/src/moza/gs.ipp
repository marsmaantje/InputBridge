// VID 0x346E  PID 0x000C
// GS (Gran Sport) Steering Wheel – GT-style round rim with 10-LED strip.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "moza_base.hpp"

namespace wheel {

class MOZA_GS : public MozaBase {
public:
    static constexpr int kLEDs = 10;
    MOZA_GS(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza GS"; }
};

} // namespace wheel
