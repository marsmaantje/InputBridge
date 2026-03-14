
#pragma once
#include <cstddef>
#include <cstdint>

namespace wheel {

class HIDTransport {
public:
    virtual ~HIDTransport() = default;

    virtual bool write(const uint8_t* data, size_t size) = 0;
    virtual bool read(uint8_t* data, size_t size) = 0;
};

}
