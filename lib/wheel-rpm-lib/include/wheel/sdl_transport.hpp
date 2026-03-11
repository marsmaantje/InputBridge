#pragma once
#include "wheel/transport.hpp"
#include <SDL3/SDL_hidapi.h>

namespace wheel {

// ---------------------------------------------------------------------------
// SDLTransport – wraps a single SDL_hid_device* in the HIDTransport interface.
//
// Cross-platform notes
// --------------------
// SDL3's HIDAPI layer is supported on Windows, macOS, and Linux without any
// additional platform guards here.  The only platform-specific concern is
// handled one level up in WheelManager::scan() when filtering HID interface
// numbers (see the comment there).
// ---------------------------------------------------------------------------

class SDLTransport : public HIDTransport {

    SDL_hid_device* dev;

public:

    explicit SDLTransport(SDL_hid_device* d) : dev(d) {}

    ~SDLTransport() override
    {
        if (dev)
            SDL_hid_close(dev);
    }

    bool write(const uint8_t* data, size_t size) override
    {
        return SDL_hid_write(dev, data, size) >= 0;
    }

    bool read(uint8_t* data, size_t size) override
    {
        return SDL_hid_read(dev, data, size) >= 0;
    }
};

} // namespace wheel
