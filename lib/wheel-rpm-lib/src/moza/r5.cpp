
#include "moza_base.hpp"

namespace wheel {

// VID 0x346E  PID 0x0005
// The R5 kit ships with the ES wheel, which has a 10-LED strip.

class MOZA_R5 : public MozaBase {

public:
    static constexpr int kLEDs = 10;
    MOZA_R5(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza R5"; }
};

}
