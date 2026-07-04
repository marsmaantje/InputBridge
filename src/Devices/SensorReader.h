#pragma once
#include "SensorState.h"
#include <SDL3/SDL.h>

/**
 * @file SensorReader.h
 * @brief Reads gyro, accelerometer, and touchpad data from SDL3 gamepad handles.
 *
 * Only sensors actually present on the device are read; capability is gated
 * through SensorCapabilities, which callers can query once on device connect
 * and cache.
 *
 * Usage per frame:
 * @code
 *   auto caps = SensorReader::QueryCapabilities(gamepad); // once on connect
 *   SensorReader::Enable(gamepad, caps);                  // once on connect
 *
 *   if (caps.gyro)   GyroState  g = SensorReader::ReadGyro(gamepad);
 *   if (caps.accel)  AccelState a = SensorReader::ReadAccel(gamepad);
 *   if (caps.touch)  TouchState t = SensorReader::ReadTouch(gamepad);
 * @endcode
 *
 * Thread-safety: all methods must be called from the main thread (SDL sensor
 * reads are not thread-safe).
 */

/// Capabilities present on a specific gamepad, queried once at connect time.
struct SensorCapabilities {
    bool gyro   = false;  ///< Single / main gyroscope
    bool accel  = false;  ///< Single / main accelerometer
    bool gyroL  = false;  ///< Left-side gyroscope  (Steam Deck)
    bool accelL = false;  ///< Left-side accelerometer
    bool gyroR  = false;  ///< Right-side gyroscope
    bool accelR = false;  ///< Right-side accelerometer
    bool touch  = false;  ///< At least one touchpad
    bool capSenseLeftStick  = false;  ///< Capacitive touch - left stick
    bool capSenseRightStick = false;  ///< Capacitive touch - right stick
    bool capSenseLeftGrip   = false;  ///< Capacitive touch - left grip
    bool capSenseRightGrip  = false;  ///< Capacitive touch - right grip

    /// Returns true if any sensor or touch input is available.
    bool HasAny() const {
        return gyro || accel || gyroL || accelL || gyroR || accelR || touch
            || capSenseLeftStick || capSenseRightStick
            || capSenseLeftGrip  || capSenseRightGrip;
    }
};

class SensorReader {
public:
    /**
     * @brief Query which sensor capabilities a gamepad supports.
     *
     * Call once when a device connects and cache the result.  Passing the
     * cached SensorCapabilities to Enable() and the Read* methods avoids
     * redundant SDL capability queries every frame.
     */
    static SensorCapabilities QueryCapabilities(SDL_Gamepad* gamepad);

    /**
     * @brief Enable all sensors reported present in @p caps.
     *
     * Safe to call every frame - SDL ignores redundant enable calls.
     */
    static void Enable(SDL_Gamepad* gamepad, const SensorCapabilities& caps);

    /**
     * @brief Enable sensors without a pre-queried capabilities struct.
     *
     * Convenience overload: queries capabilities internally and enables them.
     * Returns the discovered capabilities so callers can cache them.
     */
    static SensorCapabilities EnableAll(SDL_Gamepad* gamepad);

    // ── Motion sensors ───────────────────────────────────────────────────────

    /** @brief Read main gyroscope. Returns available=false if absent. */
    static GyroState  ReadGyro (SDL_Gamepad* gamepad);
    /** @brief Read main accelerometer. Returns available=false if absent. */
    static AccelState ReadAccel(SDL_Gamepad* gamepad);

    /** @brief Read left-side gyroscope. Returns available=false if absent. */
    static GyroState  ReadGyroL (SDL_Gamepad* gamepad);
    /** @brief Read left-side accelerometer. Returns available=false if absent. */
    static AccelState ReadAccelL(SDL_Gamepad* gamepad);

    /** @brief Read right-side gyroscope. Returns available=false if absent. */
    static GyroState  ReadGyroR (SDL_Gamepad* gamepad);
    /** @brief Read right-side accelerometer. Returns available=false if absent. */
    static AccelState ReadAccelR(SDL_Gamepad* gamepad);

    // ── Touch input ──────────────────────────────────────────────────────────

    /**
     * @brief Read touchpad finger positions.
     *
     * Finger 0 → primary pad, finger 0 (DualSense pad / Steam right pad).
     * Finger 1 → second pad finger (DualSense) or second pad (Steam left pad).
     * Returns available=false if no touchpad is present.
     */
    static TouchState ReadTouch(SDL_Gamepad* gamepad);

private:
    /// Shared implementation: read three-axis sensor data for any SDL_SensorType.
    static GyroState  ReadGyroSensor (SDL_Gamepad* gamepad, SDL_SensorType type);
    static AccelState ReadAccelSensor(SDL_Gamepad* gamepad, SDL_SensorType type);
};
