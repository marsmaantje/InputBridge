
#include "moza_base.hpp"

namespace wheel {

// VID 0x346E  PID 0x0006
// Formula Steering Rim – 16 shift-indicator LEDs (wider bar than round wheels).

class MOZA_FSR : public MozaBase {

public:

    static constexpr int kLEDs = 16;

    MOZA_FSR(std::unique_ptr<HIDTransport> t)
        : MozaBase(std::move(t), kLEDs)
    {}

    std::string name() const override { return "Moza FSR"; }
};

} // namespace wheel
