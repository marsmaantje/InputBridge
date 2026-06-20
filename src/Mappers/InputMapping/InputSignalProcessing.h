#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Stateless device-reading helpers: turn a raw SDL joystick/gamepad/sensor
// reading into the normalised [-1,1] (or [0,1]) values the rest of the
// mapping system works with.
//
// These take a `const DeviceManager&` explicitly instead of being members of
// InputMapper, so they can be unit-tested and reused by the binding listener,
// the runtime updater, and the UI without any of them needing to know about
// each other.
// ─────────────────────────────────────────────────────────────────────────────

#include "MappingTypes.h"
#include <SDL3/SDL.h>

class DeviceManager;

namespace InputMapping {

// Looks up the SDL_Joystick*/SDL_Gamepad* for a device by its current
// session instance id, or nullptr if the device isn't currently connected.
SDL_Joystick* FindJoystick(SDL_JoystickID id, const DeviceManager& dm);
SDL_Gamepad*  FindGamepad(SDL_JoystickID id, const DeviceManager& dm);

// Reads a button, decoding ButtonBinder's gamepad-only-button sentinel
// (negative indices encode an SDL_GamepadButton instead of a joystick button
// index — see ButtonBinder.cpp for the encoding).
bool ReadButtonState(SDL_JoystickID instance_id, int button_index, const DeviceManager& dm);

// Reads a regular joystick axis through `cfg`'s invert/deadzone/range options.
float ProcessAxis(const InputSource& cfg, const DeviceManager& dm);

// Reads a gamepad sensor/touchpad/battery channel through `cfg`'s
// invert/deadzone/range options. IMU (gyro/accel) channels are returned in
// their native SI-derived units rather than passed through deadzone/range,
// since the deadzone/clamp pipeline is `-1..1`-axis-shaped and would mangle
// physical units.
float ProcessSensor(const InputSource& cfg, const DeviceManager& dm);

// Convenience dispatcher used everywhere an InputSource needs to be read
// without the caller needing to branch on whether it's an axis or a sensor.
inline float ReadInputSourceValue(const InputSource& cfg, const DeviceManager& dm) {
    return cfg.sensorChannel != InputSource::SensorChannel::None
        ? ProcessSensor(cfg, dm)
        : ProcessAxis(cfg, dm);
}

} // namespace InputMapping
