
#include "moza_base.hpp"

namespace wheel {

// VID 0x346E  PID 0x000B
// 10 shift-indicator LEDs, same physical layout as R9.

class MOZA_R12 : public MozaBase {

public:
    static constexpr int kLEDs = 10;
    MOZA_R12(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza R12"; }
};

}
