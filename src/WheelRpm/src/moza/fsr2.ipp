// VID 0x346E  PID 0x0013
// FSR2 (Formula Steering Rim 2) – updated Formula rim, same 16-LED strip as FSR.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "moza_base.hpp"

namespace wheel {

class MOZA_FSR2 : public MozaBase {
public:
    static constexpr int kLEDs = 16;
    MOZA_FSR2(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza FSR2"; }
};

} // namespace wheel
