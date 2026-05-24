#pragma once
#include "SensorState.h"
#include <SDL3/SDL.h>

/**
 * @file SensorReader.h
 * @brief Reads gyro, accelerometer, and touchpad data from SDL3 gamepad handles.
 *
 * Usage per frame:
 * @code
 *   SensorReader reader;
 *   if (reader.Enable(gamepad))  {              // call once on device connect
 *       GyroState  g = reader.ReadGyro(gamepad);
 *       AccelState a = reader.ReadAccel(gamepad);
 *       TouchState t = reader.ReadTouch(gamepad);
 *   }
 * @endcode
 *
 * Enable() calls SDL_SetGamepadSensorEnabled() for both sensor types.
 * If a sensor is absent, the corresponding State::available flag is false.
 *
 * Thread-safety: all methods must be called from the main thread (SDL
 * sensor reads are not thread-safe).
 */
class SensorReader {
public:
    /**
     * @brief Enable sensors on a gamepad.
     *
     * Safe to call every frame — SDL ignores redundant enable calls.
     * Returns true if at least one sensor or touchpad is available.
     */
    static bool Enable(SDL_Gamepad* gamepad);

    /** @brief Read the gyroscope. Returns all-zero with available=false if absent. */
    static GyroState  ReadGyro (SDL_Gamepad* gamepad);

    /** @brief Read the accelerometer. Returns all-zero with available=false if absent. */
    static AccelState ReadAccel(SDL_Gamepad* gamepad);

    /**
     * @brief Read touchpad finger positions.
     *
     * Reads pad index 0 (the main touchpad on DualSense and Steam Controller).
     * Returns available=false if no touchpad is present.
     */
    static TouchState ReadTouch(SDL_Gamepad* gamepad);
};
