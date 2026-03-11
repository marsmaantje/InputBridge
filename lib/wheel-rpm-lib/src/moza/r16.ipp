// VID 0x346E  PID 0x000D
// R16 (16 Nm) – 10 shift-indicator LEDs, same protocol as R9/R12.
// PID confirmed via community HID sniff; see:
//   https://github.com/moza-racingwheel/moza-protocol

#include "moza_base.hpp"

namespace wheel {

class MOZA_R16 : public MozaBase {
public:
    static constexpr int kLEDs = 10;
    MOZA_R16(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza R16"; }
};

} // namespace wheel
