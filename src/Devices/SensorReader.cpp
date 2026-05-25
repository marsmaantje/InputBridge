#include "SensorReader.h"
#include <algorithm>
#include <cmath>

bool SensorReader::Enable(SDL_Gamepad* gamepad) {
    if (!gamepad) return false;

    bool any = false;

    if (SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO)) {
        SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true);
        any = true;
    }
    if (SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL)) {
        SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, true);
        any = true;
    }
    if (SDL_GetNumGamepadTouchpads(gamepad) > 0) {
        any = true;
    }

    return any;
}

GyroState SensorReader::ReadGyro(SDL_Gamepad* gamepad) {
    GyroState state;
    if (!gamepad) return state;
    if (!SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO)) return state;

    float data[3] = {0.f, 0.f, 0.f};
    if (!SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_GYRO, data, 3)) return state;

    state.available = true;
    // Normalise rad/s → [-1, 1] and clamp.
    state.x = std::clamp(data[0] / GyroState::SCALE, -1.f, 1.f);
    state.y = std::clamp(data[1] / GyroState::SCALE, -1.f, 1.f);
    state.z = std::clamp(data[2] / GyroState::SCALE, -1.f, 1.f);
    return state;
}

AccelState SensorReader::ReadAccel(SDL_Gamepad* gamepad) {
    AccelState state;
    if (!gamepad) return state;
    if (!SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL)) return state;

    float data[3] = {0.f, 0.f, 0.f};
    if (!SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_ACCEL, data, 3)) return state;

    state.available = true;
    state.x = std::clamp(data[0] / AccelState::SCALE, -1.f, 1.f);
    state.y = std::clamp(data[1] / AccelState::SCALE, -1.f, 1.f);
    state.z = std::clamp(data[2] / AccelState::SCALE, -1.f, 1.f);
    return state;
}

TouchState SensorReader::ReadTouch(SDL_Gamepad* gamepad) {
    TouchState state;
    if (!gamepad) return state;

    int numPads = SDL_GetNumGamepadTouchpads(gamepad);
    if (numPads <= 0) return state;

    state.available = true;

    // Mapping logic:
    // Finger 0: Main pad (DualSense pad, or Steam Right pad)
    // Finger 1: Secondary source (Steam Left pad if it exists, else DualSense second finger)

    // Touchpad 0, Finger 0 (Always used for the primary UI mapping)
    if (SDL_GetNumGamepadTouchpadFingers(gamepad, 0) > 0) {
        SDL_GetGamepadTouchpadFinger(gamepad, 0, 0,
            &state.fingers[0].active,
            &state.fingers[0].x,
            &state.fingers[0].y,
            &state.fingers[0].pressure);
    }

    // Map the secondary UI slot (Touch 2)
    if (numPads > 1) {
        // Steam Controller / Steam Deck: Use the first finger of the Left Pad (Touchpad 1)
        if (SDL_GetNumGamepadTouchpadFingers(gamepad, 1) > 0) {
            SDL_GetGamepadTouchpadFinger(gamepad, 1, 0,
                &state.fingers[1].active,
                &state.fingers[1].x,
                &state.fingers[1].y,
                &state.fingers[1].pressure);
        }
    } else if (SDL_GetNumGamepadTouchpadFingers(gamepad, 0) > 1) {
        // Single Pad (DualSense): Use the second finger of the main pad
        SDL_GetGamepadTouchpadFinger(gamepad, 0, 1,
            &state.fingers[1].active,
            &state.fingers[1].x,
            &state.fingers[1].y,
            &state.fingers[1].pressure);
    }

    return state;
}