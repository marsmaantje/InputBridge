
#include "wheel/wheel.hpp"

namespace wheel {

class MOZA_R9 : public Wheel {

public:

    bool setRPM(float percent) override
    {
        (void)percent;
        return false;
    }

    bool setLEDs(const std::vector<uint8_t>& leds) override
    {
        (void)leds;
        return false;
    }

    std::string name() const override
    {
        return "Moza R9";
    }
};

}
