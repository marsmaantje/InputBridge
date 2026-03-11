// VID 0x346E  PID 0x0008
// CS (Compact Sport) Steering Wheel – used on R5/R9/R12 bases.
// Features a 10-LED shift-indicator strip.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "moza_base.hpp"

namespace wheel {

class MOZA_CS : public MozaBase {
public:
    static constexpr int kLEDs = 10;
    MOZA_CS(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza CS"; }
};

} // namespace wheel
