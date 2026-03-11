// VID 0x346E  PID 0x000F
// R21 (21 Nm) – 10 shift-indicator LEDs.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "moza_base.hpp"

namespace wheel {

class MOZA_R21 : public MozaBase {
public:
    static constexpr int kLEDs = 10;
    MOZA_R21(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza R21"; }
};

} // namespace wheel
