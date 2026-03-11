// VID 0x346E  PID 0x0009
// 10 shift-indicator LEDs arranged left-to-right on the steering wheel rim.

#include "moza_base.hpp"

namespace wheel {

class MOZA_R9 : public MozaBase {
public:
    static constexpr int kLEDs = 10;
    MOZA_R9(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza R9"; }
};

} // namespace wheel
