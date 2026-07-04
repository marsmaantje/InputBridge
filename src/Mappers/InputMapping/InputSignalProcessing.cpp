#include "InputSignalProcessing.h"
#include "Devices/DeviceManager.h"
#include "Devices/SensorReader.h"
#include <algorithm>
#include <cmath>

namespace InputMapping {

namespace {

// Remaps `r` (already clamped to [-1, 1]) into the field's configured output
// range. Shared by the axis pipeline and the non-IMU sensor pipeline below -
// the two pipelines differ in how they apply the deadzone (see
// ApplyAxisDeadzone's comment) but converge on this final remap.
float ApplyOutputRange(float r, int outputRange, float customMin, float customMax) {
    switch (outputRange) {
        case 1: return (r + 1.f) * 0.5f;        // 0..1
        case 2: return (r - 1.f) * 0.5f;        // -1..0
        case 3: return std::max(r, 0.f);        // +half: 0 at centre, 1 at max positive
        case 4: return std::max(-r, 0.f);       // -half: 0 at centre, 1 at max negative
        case 5: return customMin + (r + 1.f) * 0.5f * (customMax - customMin); // custom
        default: return r;                      // -1..1
    }
}

// Joystick axes get a deadzone with a rescale, so input directly outside the
// deadzone still reaches the full -1..1 range instead of jumping from 0.
float ApplyAxisDeadzone(float norm, float deadzone) {
    if (std::abs(norm) < deadzone) return 0.f;
    return norm > 0 ? (norm - deadzone) / (1.f - deadzone)
                     : (norm + deadzone) / (1.f - deadzone);
}

// Picks the x/y/z component of a 3-axis sensor reading (GyroState/AccelState)
// matching the requested channel. Replaces the repeated
// "ch == X ? v.x : ch == Y ? v.y : v.z" ternary chains that appeared once per
// IMU group in the original switch statement.
template <typename Vec3>
float SelectComponent(InputSource::SensorChannel ch, InputSource::SensorChannel chX,
                       InputSource::SensorChannel chY, const Vec3& v) {
    if (ch == chX) return v.x;
    if (ch == chY) return v.y;
    return v.z;
}

float ReadTouchChannel(InputSource::SensorChannel ch, const TouchState& t) {
    using SC = InputSource::SensorChannel;
    switch (ch) {
        case SC::TouchX:         return t.fingers[0].active ? t.primaryXCentered() : 0.f;
        case SC::TouchY:         return t.fingers[0].active ? t.primaryYCentered() : 0.f;
        case SC::TouchPressure:  return t.fingers[0].active ? t.primaryPressure()  : 0.f;
        case SC::Touch2X:        return t.fingers[1].active ? (t.fingers[1].x * 2.f - 1.f) : 0.f;
        case SC::Touch2Y:        return t.fingers[1].active ? (t.fingers[1].y * 2.f - 1.f) : 0.f;
        case SC::Touch2Pressure: return t.fingers[1].active ? t.fingers[1].pressure : 0.f;
        default:                 return 0.f;
    }
}

float ReadCapSenseChannel(InputSource::SensorChannel ch, SDL_Gamepad* gamepad) {
    using SC = InputSource::SensorChannel;
    switch (ch) {
        case SC::LeftStickTouch:  return SDL_GetGamepadCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_LEFT_STICK)  ? 1.f : 0.f;
        case SC::RightStickTouch: return SDL_GetGamepadCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_RIGHT_STICK) ? 1.f : 0.f;
        case SC::LeftGripTouch:   return SDL_GetGamepadCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_LEFT_GRIP)   ? 1.f : 0.f;
        case SC::RightGripTouch:  return SDL_GetGamepadCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_RIGHT_GRIP)  ? 1.f : 0.f;
        default:                  return 0.f;
    }
}

// Reads the raw (pre invert/deadzone) value for every non-battery sensor
// channel. Battery channels are handled separately in ProcessSensor because
// they don't require a gamepad handle or IMU enablement.
float ReadGamepadSensorChannel(InputSource::SensorChannel ch, SDL_Gamepad* gamepad) {
    using SC = InputSource::SensorChannel;

    // Enable sensors each frame - SDL ignores redundant calls, cost is negligible.
    SensorReader::EnableAll(gamepad);

    switch (ch) {
        case SC::GyroX: case SC::GyroY: case SC::GyroZ:
            return SelectComponent(ch, SC::GyroX, SC::GyroY, SensorReader::ReadGyro(gamepad));
        case SC::AccelX: case SC::AccelY: case SC::AccelZ:
            return SelectComponent(ch, SC::AccelX, SC::AccelY, SensorReader::ReadAccel(gamepad));
        case SC::GyroLX: case SC::GyroLY: case SC::GyroLZ:
            return SelectComponent(ch, SC::GyroLX, SC::GyroLY, SensorReader::ReadGyroL(gamepad));
        case SC::AccelLX: case SC::AccelLY: case SC::AccelLZ:
            return SelectComponent(ch, SC::AccelLX, SC::AccelLY, SensorReader::ReadAccelL(gamepad));
        case SC::GyroRX: case SC::GyroRY: case SC::GyroRZ:
            return SelectComponent(ch, SC::GyroRX, SC::GyroRY, SensorReader::ReadGyroR(gamepad));
        case SC::AccelRX: case SC::AccelRY: case SC::AccelRZ:
            return SelectComponent(ch, SC::AccelRX, SC::AccelRY, SensorReader::ReadAccelR(gamepad));
        case SC::TouchX: case SC::TouchY: case SC::TouchPressure:
        case SC::Touch2X: case SC::Touch2Y: case SC::Touch2Pressure:
            return ReadTouchChannel(ch, SensorReader::ReadTouch(gamepad));
        case SC::LeftStickTouch: case SC::RightStickTouch:
        case SC::LeftGripTouch:  case SC::RightGripTouch:
            return ReadCapSenseChannel(ch, gamepad);
        default:
            return 0.f;
    }
}

bool IsImuChannel(InputSource::SensorChannel ch) {
    using SC = InputSource::SensorChannel;
    return (ch >= SC::GyroX && ch <= SC::AccelZ) || (ch >= SC::GyroLX && ch <= SC::AccelRZ);
}

} // namespace

SDL_Joystick* FindJoystick(SDL_JoystickID id, const DeviceManager& dm) {
    if (id == 0) return nullptr;
    for (const auto& d : dm.GetDevices())
        if (d.instance_id == id) return d.joystick;
    return nullptr;
}

SDL_Gamepad* FindGamepad(SDL_JoystickID id, const DeviceManager& dm) {
    if (id == 0) return nullptr;
    for (const auto& d : dm.GetDevices())
        if (d.instance_id == id) return d.gamepad;
    return nullptr;
}

bool ReadButtonState(SDL_JoystickID instance_id, int button_index, const DeviceManager& dm) {
    if (button_index < 0) {
        // Gamepad-only button (paddle sentinel): stored_index = -(SDL_GamepadButton + 1).
        SDL_Gamepad* gp = FindGamepad(instance_id, dm);
        if (!gp) return false;
        SDL_GamepadButton btn = static_cast<SDL_GamepadButton>(-(button_index + 1));
        return SDL_GetGamepadButton(gp, btn) != 0;
    }
    SDL_Joystick* j = FindJoystick(instance_id, dm);
    if (!j) return false;
    return SDL_GetJoystickButton(j, button_index) != 0;
}

float ProcessAxis(const InputSource& cfg, const DeviceManager& dm) {
    if (cfg.axisIndex < 0 || cfg.instance_id == 0) return 0.f;
    SDL_Joystick* j = FindJoystick(cfg.instance_id, dm);
    if (!j) return 0.f;
    Sint16 raw = SDL_GetJoystickAxis(j, cfg.axisIndex);
    float norm = raw < 0 ? (float)raw / 32768.f : (float)raw / 32767.f;
    if (cfg.invert) norm = -norm;
    norm = ApplyAxisDeadzone(norm, cfg.deadzone);
    float r = std::clamp(norm, -1.f, 1.f);
    return ApplyOutputRange(r, cfg.outputRange, cfg.customRangeMin, cfg.customRangeMax);
}

float ProcessSensor(const InputSource& cfg, const DeviceManager& dm) {
    using SC = InputSource::SensorChannel;
    if (cfg.sensorChannel == SC::None || cfg.instance_id == 0) return 0.f;

    const DeviceState* devState = nullptr;
    for (const auto& dev : dm.GetDevices()) {
        if (dev.instance_id == cfg.instance_id) { devState = &dev; break; }
    }
    if (!devState) return 0.f;

    float raw = 0.f;

    // Battery channels do not require a gamepad handle or IMU enablement.
    if (cfg.sensorChannel == SC::BatteryLevel) {
        raw = (devState->battery_percent >= 0) ? (float)devState->battery_percent / 100.f : 0.f;
    } else if (cfg.sensorChannel == SC::BatteryCharging) {
        bool charging = (devState->battery_state == SDL_POWERSTATE_CHARGING ||
                         devState->battery_state == SDL_POWERSTATE_CHARGED);
        raw = charging ? 1.f : 0.f;
    } else if (devState->gamepad) {
        raw = ReadGamepadSensorChannel(cfg.sensorChannel, devState->gamepad);
    }

    if (cfg.invert) raw = -raw;

    // For IMU sensors (Gyro/Accel), values are in SI units (rad/s or m/s^2).
    // Applying the default axis deadzone (0.05) or clamping to [-1, 1] destroys the data.
    if (IsImuChannel(cfg.sensorChannel)) return raw;

    if (std::abs(raw) < cfg.deadzone) raw = 0.f;
    float r = std::clamp(raw, -1.f, 1.f);
    return ApplyOutputRange(r, cfg.outputRange, cfg.customRangeMin, cfg.customRangeMax);
}

} // namespace InputMapping