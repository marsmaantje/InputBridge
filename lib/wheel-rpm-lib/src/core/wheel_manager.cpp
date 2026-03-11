#include "wheel/wheel_manager.hpp"
#include "wheel/transport.hpp"
#include "wheel/sdl_transport.hpp"

// Moza concrete types – included here so the translation unit owns them.
// Each .cpp defines a class in namespace wheel; we forward-declare and
// instantiate them via a factory table rather than exposing their headers.
#include "../moza/moza_base.hpp"
#include "../moza/r3.ipp"
#include "../moza/r5.ipp"
#include "../moza/r9.ipp"
#include "../moza/r12.ipp"
#include "../moza/fsr.ipp"

#include <SDL3/SDL_hidapi.h>
#include <memory>

// VID/PID detection placeholder
// Example mapping:
// Logitech G29: 046D:C24F
// Logitech G923: 046D:C266
// Fanatec CSL DD: 0EB7:0005
// Moza R9: 346E:0009

namespace wheel {

// ---------------------------------------------------------------------------
// VID / PID table for Moza devices
// All Moza bases share VID 0x346E.  The USB interface used for HID LED
// control is interface 0 (the default HID usage page exposed by the base).
// PIDs confirmed via community HID sniffing; see comments in each wheel file.
// ---------------------------------------------------------------------------

static constexpr uint16_t kMozaVID = 0x346E;

struct MozaEntry {
    uint16_t pid;
    const char* label;                   // human-readable, matches name()
    std::unique_ptr<Wheel>(*make)(std::unique_ptr<HIDTransport>);
};

// Factory helpers: each lambda opens HID, wraps it, constructs the wheel.
static std::unique_ptr<Wheel> makeMozaR3 (std::unique_ptr<HIDTransport> t) { return std::make_unique<MOZA_R3> (std::move(t)); }
static std::unique_ptr<Wheel> makeMozaR5 (std::unique_ptr<HIDTransport> t) { return std::make_unique<MOZA_R5> (std::move(t)); }
static std::unique_ptr<Wheel> makeMozaR9 (std::unique_ptr<HIDTransport> t) { return std::make_unique<MOZA_R9> (std::move(t)); }
static std::unique_ptr<Wheel> makeMozaR12(std::unique_ptr<HIDTransport> t) { return std::make_unique<MOZA_R12>(std::move(t)); }
static std::unique_ptr<Wheel> makeMozaFSR(std::unique_ptr<HIDTransport> t) { return std::make_unique<MOZA_FSR>(std::move(t)); }

static const MozaEntry kMozaTable[] = {
    { 0x0001, "Moza R3",  makeMozaR3  },
    { 0x0005, "Moza R5",  makeMozaR5  },
    { 0x0009, "Moza R9",  makeMozaR9  },
    { 0x000B, "Moza R12", makeMozaR12 },
    { 0x0006, "Moza FSR", makeMozaFSR },
};

// ---------------------------------------------------------------------------
// WheelManager::scan
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<Wheel>> WheelManager::scan()
{
    std::vector<std::unique_ptr<Wheel>> wheels;

    SDL_hid_device_info* devs = SDL_hid_enumerate(0, 0);

    for (auto d = devs; d; d = d->next)
    {
        // --- Moza ---
        if (d->vendor_id == kMozaVID)
        {
            for (const auto& entry : kMozaTable)
            {
                if (d->product_id != entry.pid)
                    continue;

                // Moza bases may expose multiple HID interfaces.
                // Interface 0 is the primary joystick/control interface that
                // accepts LED output reports.
                if (d->interface_number != 0)
                    continue;

                SDL_hid_device* hdev = SDL_hid_open_path(d->path);
                if (!hdev)
                    continue;  // device busy or permissions issue – skip

                // Set non-blocking so reads don't stall the UI thread.
                SDL_hid_set_nonblocking(hdev, 1);

                auto transport = std::make_unique<SDLTransport>(hdev);
                auto wheel     = entry.make(std::move(transport));
                wheels.push_back(std::move(wheel));
                break; // don't match the same device twice
            }
            continue;
        }

        // Future backends (Fanatec, Logitech, Thrustmaster, …) would be
        // dispatched here with their own VID checks:
        //
        // if (d->vendor_id == kFanatecVID) { ... }
        // if (d->vendor_id == kLogitechVID) { ... }

        (void)d;
    }

    SDL_hid_free_enumeration(devs);

    return wheels;
}

} // namespace wheel
