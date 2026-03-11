
#include "fanatec_base.hpp"
#include "wheel/utils/rpm_mapper.hpp"

namespace wheel {

class FANATEC_CSL_DD : public FanatecWheel {

public:

    using FanatecWheel::FanatecWheel;

    bool setRPM(float percent) override
    {
        uint8_t report[16]{};

        report[0] = 0x08;

        auto leds = RPMMapper::linear(percent,9);

        for(size_t i=0;i<leds.size();i++)
            report[i+1] = leds[i] ? 0xFF : 0;

        return hid->write(report,10);
    }

    bool setLEDs(const std::vector<uint8_t>& leds) override
    {
        uint8_t report[16]{};

        report[0] = 0x08;

        for(size_t i=0;i<leds.size() && i<9;i++)
            report[i+1] = leds[i];

        return hid->write(report,10);
    }

    std::string name() const override
    {
        return "Fanatec CSL_DD";
    }
};

}
