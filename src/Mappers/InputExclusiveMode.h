#pragma once

#include <memory>
#include <SDL3/SDL.h>

class InputExclusiveModeImpl;

// High-level device-hide manager.
//
// Usage:
//   InputExclusiveMode mgr;
//   if (mgr.IsAvailable())
//       mgr.SetHidden(joystick, true);   // hide from other apps
//       mgr.SetHidden(joystick, false);  // unhide
//
// Steam Input compatibility (Windows / HidHide only):
//   mgr.SetSteamInputCompatible(true);  // keep Steam in the allow-list
class InputExclusiveMode {
public:
    InputExclusiveMode();
    ~InputExclusiveMode();

    // Returns true when the underlying platform mechanism is available.
    bool IsAvailable() const;

    // Hide or unhide a single device.  Returns true on success.
    bool SetHidden(SDL_Joystick* joystick, bool hidden);

    // Allow/disallow Steam to still access hidden devices (Windows only).
    void SetSteamInputCompatible(bool enabled);

private:
    std::unique_ptr<InputExclusiveModeImpl> m_Impl;
};