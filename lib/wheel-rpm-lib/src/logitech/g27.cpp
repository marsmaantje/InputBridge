
#include "logitech_base.hpp"
#include "wheel/utils/rpm_mapper.hpp"

namespace wheel {

class LOGI_G27 : public LogitechWheel {

public:

    using LogitechWheel::LogitechWheel;

    bool setRPM(float percent) override
    {
        auto leds = RPMMapper::linear(percent,5);

        uint8_t mask = 0;

        for(size_t i=0;i<leds.size();i++)
            if(leds[i])
                mask |= (1<<i);

        return sendMask(mask);
    }

    bool setLEDs(const std::vector<uint8_t>& leds) override
    {
        uint8_t mask = 0;

        for(size_t i=0;i<leds.size() && i<5;i++)
            if(leds[i])
                mask |= (1<<i);

        return sendMask(mask);
    }

    std::string name() const override
    {
        return "Logitech G27";
    }
};

}
