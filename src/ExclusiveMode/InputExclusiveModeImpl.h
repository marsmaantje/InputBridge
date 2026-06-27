#pragma once

#include <SDL3/SDL.h>

// Abstract platform backend for the device-hide feature.
//
// Each platform implements hide/unhide using the best available mechanism:
//   Windows – HidHide kernel driver (https://github.com/nefarius/HidHide)
//   Linux   – EVIOCGRAB exclusive evdev grab (per-device)
//   macOS   – IOHIDOptionsTypeSeizeDevice
class InputExclusiveModeImpl {
public:
    virtual ~InputExclusiveModeImpl() = default;

    // Hide this specific device from all other applications.
    // InputBridge (and optionally Steam on Windows) retains access.
    // Returns true on success.
    virtual bool HideDevice(SDL_Joystick* joystick) = 0;

    // Undo a previous HideDevice call for this joystick handle.
    // Returns true on success.
    virtual bool UnhideDevice(SDL_Joystick* joystick) = 0;

    // True when the underlying driver/mechanism is present and functional.
    virtual bool IsAvailable() const = 0;

    // Optional: add (true) or remove (false) Steam from the per-process
    // allow-list so that Steam Input can still read the physical device even
    // while it is hidden from regular applications.  The default no-op
    // implementation is fine for Linux / macOS where the grab already allows
    // SDL (and therefore InputBridge) through at the process level.
    virtual void SetSteamInputCompatible(bool /*enabled*/) {}
};