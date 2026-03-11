
#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace wheel {

class Wheel {
public:
    virtual ~Wheel() = default;

    virtual bool setRPM(float percent) = 0;
    virtual bool setLEDs(const std::vector<uint8_t>& leds) = 0;
    virtual bool setGear(int gear) { (void)gear; return false; }
    virtual bool setFlag(int flag) { (void)flag; return false; }

    virtual std::string name() const = 0;
};

}
