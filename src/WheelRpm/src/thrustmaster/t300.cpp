
#include "wheel/wheel.hpp"

namespace wheel {

class TM_T300 : public Wheel {

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
        return "Thrustmaster T300";
    }
};

}
