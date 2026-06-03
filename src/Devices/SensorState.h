#pragma once
#include <cstdint>
#include <array>

/**
 * @file SensorState.h
 * @brief Normalised sensor and touchpad state structs.
 *
 * All values are normalised to [-1, 1] or [0, 1] so the rest of the
 * application doesn't have to know raw SDL units:
 *
 * - GyroState:  rad/s divided by GYRO_SCALE  → [-1, 1]
 * - AccelState: m/s² divided by ACCEL_SCALE  → [-1, 1]  (gravity ≈ 0.1 at rest)
 * - TouchState: x/y already [0,1] from SDL;  pressure [0,1];  active bool.
 *
 * These structs are value types — cheap to copy and hold no SDL handles.
 */

struct GyroState {
    float x = 0.f;  ///< Pitch rate  (rad/s → [-1, 1])
    float y = 0.f;  ///< Yaw rate    (rad/s → [-1, 1])
    float z = 0.f;  ///< Roll rate   (rad/s → [-1, 1])
    bool  available = false;

    /// Full-scale range used for normalisation (rad/s).
    /// DualSense gyro saturates around ±35 rad/s; use ±20 as a useful range.
    static constexpr float SCALE = 20.f;
};

struct AccelState {
    float x = 0.f;  ///< Lateral    (m/s² → [-1, 1])
    float y = 0.f;  ///< Vertical   (m/s² → [-1, 1])
    float z = 0.f;  ///< Fore/aft   (m/s² → [-1, 1])
    bool  available = false;

    /// Full-scale range (m/s²).  Gravity is ~9.81; use ±20 so ±1g ≈ 0.5.
    static constexpr float SCALE = 20.f;
};

struct TouchFingerState {
    bool  active   = false;
    float x        = 0.f;  ///< [0, 1] left→right
    float y        = 0.f;  ///< [0, 1] top→bottom
    float pressure = 0.f;  ///< [0, 1]
};

/// Up to 2 touch fingers (DualSense has one touchpad with 2-finger support).
struct TouchState {
    bool available = false;
    std::array<TouchFingerState, 2> fingers;

    /// Convenience: x/y of the first active finger, or 0 if none.
    float primaryX()        const { return fingers[0].active ? fingers[0].x : 0.f; }
    float primaryY()        const { return fingers[0].active ? fingers[0].y : 0.f; }
    float primaryPressure() const { return fingers[0].active ? fingers[0].pressure : 0.f; }

    /// Returns finger x remapped to [-1, 1] (left=-1, right=+1).
    float primaryXCentered() const { return primaryX() * 2.f - 1.f; }
    /// Returns finger y remapped to [-1, 1] (top=-1, bottom=+1).
    float primaryYCentered() const { return primaryY() * 2.f - 1.f; }
};
