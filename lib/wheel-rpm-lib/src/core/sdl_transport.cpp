
#include "wheel/transport.hpp"
#include <SDL3/SDL_hidapi.h>

namespace wheel {

class SDLTransport : public HIDTransport {

    SDL_hid_device* dev;

public:

    SDLTransport(SDL_hid_device* d)
        : dev(d) {}

    bool write(const uint8_t* data, size_t size) override
    {
        return SDL_hid_write(dev, data, size) >= 0;
    }

    bool read(uint8_t* data, size_t size) override
    {
        return SDL_hid_read(dev, data, size) >= 0;
    }

};

}
