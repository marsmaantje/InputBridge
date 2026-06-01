#include "SensorReader.h"
#include <algorithm>

// ── Private helpers ───────────────────────────────────────────────────────────
//
// All six Read*Gyro / Read*Accel public methods previously duplicated the same
// three-line read-and-normalise body.  A single private implementation per
// sensor family eliminates that duplication.

GyroState SensorReader::ReadGyroSensor(SDL_Gamepad* gamepad, SDL_SensorType type) {
    GyroState state;
    if (!gamepad) return state;
    if (!SDL_GamepadHasSensor(gamepad, type)) return state;

    float data[3] = {};
    if (!SDL_GetGamepadSensorData(gamepad, type, data, 3)) return state;

    state.available = true;
    state.x = std::clamp(data[0] / GyroState::SCALE, -1.f, 1.f);
    state.y = std::clamp(data[1] / GyroState::SCALE, -1.f, 1.f);
    state.z = std::clamp(data[2] / GyroState::SCALE, -1.f, 1.f);
    return state;
}

AccelState SensorReader::ReadAccelSensor(SDL_Gamepad* gamepad, SDL_SensorType type) {
    AccelState state;
    if (!gamepad) return state;
    if (!SDL_GamepadHasSensor(gamepad, type)) return state;

    float data[3] = {};
    if (!SDL_GetGamepadSensorData(gamepad, type, data, 3)) return state;

    state.available = true;
    state.x = std::clamp(data[0] / AccelState::SCALE, -1.f, 1.f);
    state.y = std::clamp(data[1] / AccelState::SCALE, -1.f, 1.f);
    state.z = std::clamp(data[2] / AccelState::SCALE, -1.f, 1.f);
    return state;
}

// ── Capability query ──────────────────────────────────────────────────────────

SensorCapabilities SensorReader::QueryCapabilities(SDL_Gamepad* gamepad) {
    SensorCapabilities caps;
    if (!gamepad) return caps;

    caps.gyro   = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO);
    caps.accel  = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL);
    caps.gyroL  = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO_L);
    caps.accelL = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL_L);
    caps.gyroR  = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO_R);
    caps.accelR = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL_R);
    caps.touch  = SDL_GetNumGamepadTouchpads(gamepad) > 0;

    caps.capSenseLeftStick  = SDL_GamepadHasCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_LEFT_STICK);
    caps.capSenseRightStick = SDL_GamepadHasCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_RIGHT_STICK);
    caps.capSenseLeftGrip   = SDL_GamepadHasCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_LEFT_GRIP);
    caps.capSenseRightGrip  = SDL_GamepadHasCapSense(gamepad, SDL_GAMEPAD_CAPSENSE_RIGHT_GRIP);

    return caps;
}

// ── Enable ────────────────────────────────────────────────────────────────────

void SensorReader::Enable(SDL_Gamepad* gamepad, const SensorCapabilities& caps) {
    if (!gamepad) return;

    // Only enable sensors the device actually has — SDL ignores redundant calls,
    // but being explicit avoids noise in SDL's internal bookkeeping.
    static constexpr struct { bool SensorCapabilities::*flag; SDL_SensorType type; } kMotionSensors[] = {
        { &SensorCapabilities::gyro,   SDL_SENSOR_GYRO    },
        { &SensorCapabilities::accel,  SDL_SENSOR_ACCEL   },
        { &SensorCapabilities::gyroL,  SDL_SENSOR_GYRO_L  },
        { &SensorCapabilities::accelL, SDL_SENSOR_ACCEL_L },
        { &SensorCapabilities::gyroR,  SDL_SENSOR_GYRO_R  },
        { &SensorCapabilities::accelR, SDL_SENSOR_ACCEL_R },
    };

    for (const auto& entry : kMotionSensors) {
        if (caps.*entry.flag)
            SDL_SetGamepadSensorEnabled(gamepad, entry.type, true);
    }
}

SensorCapabilities SensorReader::EnableAll(SDL_Gamepad* gamepad) {
    auto caps = QueryCapabilities(gamepad);
    Enable(gamepad, caps);
    return caps;
}

// ── Public read interface — delegates to shared helpers ───────────────────────

GyroState  SensorReader::ReadGyro (SDL_Gamepad* g) { return ReadGyroSensor (g, SDL_SENSOR_GYRO);    }
AccelState SensorReader::ReadAccel(SDL_Gamepad* g) { return ReadAccelSensor(g, SDL_SENSOR_ACCEL);   }
GyroState  SensorReader::ReadGyroL(SDL_Gamepad* g) { return ReadGyroSensor (g, SDL_SENSOR_GYRO_L);  }
AccelState SensorReader::ReadAccelL(SDL_Gamepad* g){ return ReadAccelSensor(g, SDL_SENSOR_ACCEL_L); }
GyroState  SensorReader::ReadGyroR(SDL_Gamepad* g) { return ReadGyroSensor (g, SDL_SENSOR_GYRO_R);  }
AccelState SensorReader::ReadAccelR(SDL_Gamepad* g){ return ReadAccelSensor(g, SDL_SENSOR_ACCEL_R); }

// ── Touch ─────────────────────────────────────────────────────────────────────

TouchState SensorReader::ReadTouch(SDL_Gamepad* gamepad) {
    TouchState state;
    if (!gamepad) return state;

    const int numPads = SDL_GetNumGamepadTouchpads(gamepad);
    if (numPads <= 0) return state;

    state.available = true;

    // Finger slot 0: primary pad, primary finger.
    if (SDL_GetNumGamepadTouchpadFingers(gamepad, 0) > 0) {
        auto& f = state.fingers[0];
        SDL_GetGamepadTouchpadFinger(gamepad, 0, 0, &f.active, &f.x, &f.y, &f.pressure);
    }

    // Finger slot 1: second pad (Steam) or second finger on the same pad (DualSense).
    const int secondPad    = (numPads > 1) ? 1 : 0;
    const int secondFinger = (numPads > 1) ? 0 : 1;
    if (SDL_GetNumGamepadTouchpadFingers(gamepad, secondPad) > secondFinger) {
        auto& f = state.fingers[1];
        SDL_GetGamepadTouchpadFinger(gamepad, secondPad, secondFinger,
                                     &f.active, &f.x, &f.y, &f.pressure);
    }

    return state;
}
