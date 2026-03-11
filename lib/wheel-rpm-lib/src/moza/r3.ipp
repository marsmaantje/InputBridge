// VID 0x346E  PID 0x0001
// The R3 kit ships with a compact round rim that has a 10-LED strip.

#include "moza_base.hpp"

namespace wheel {

class MOZA_R3 : public MozaBase {
public:
    static constexpr int kLEDs = 10;
    MOZA_R3(std::unique_ptr<HIDTransport> t) : MozaBase(std::move(t), kLEDs) {}
    std::string name() const override { return "Moza R3"; }
};

} // namespace wheel
