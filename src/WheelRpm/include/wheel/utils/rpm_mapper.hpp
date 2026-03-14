
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace wheel {

class RPMMapper {
public:

    static std::vector<uint8_t> linear(float percent, int ledCount)
    {
        if(percent < 0) percent = 0;
        if(percent > 1) percent = 1;

        int active = static_cast<int>(std::round(percent * ledCount));

        std::vector<uint8_t> leds(ledCount,0);

        for(int i=0;i<active;i++)
            leds[i] = 1;

        return leds;
    }

};

}
