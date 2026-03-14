// VID 0x346E  PID 0x0007
// ES (Endurance Sport) Steering Wheel – oval endurance-style rim with 10-LED strip.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "moza_base.hpp"

namespace wheel {

class MOZA_ES : public MozaBase {
public:
    static constexpr int kLEDs = 10;
    MOZA_ES(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza ES"; }
};

} // namespace wheel
