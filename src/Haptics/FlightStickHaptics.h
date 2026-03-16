#pragma once
#include "Haptics/HapticDevice.h"

/**
 * @class FlightStickHaptics
 * @brief Haptic feedback implementation for flight sticks and throttle controllers.
 *
 * Flight sticks with force-feedback hardware (e.g. Logitech G940, older
 * ThrustMaster units) typically expose:
 *   - SDL_HAPTIC_CONSTANT  – constant-force on X and/or Y axis
 *   - SDL_HAPTIC_PERIODIC  – vibration / buffet effects
 *   - HapticConditionType::Spring / Damper / Inertia / Friction
 *                          – position-dependent condition effects (centering, drag, …)
 *
 * Devices without dedicated haptic hardware (SDL_IsJoystickHaptic() == false)
 * will never reach this class; DeviceFactory filters them out before
 * construction.
 *
 * API mirrors SteeringWheelHaptics so callers can treat both uniformly through
 * the HapticDevice base interface.
 */
class FlightStickHaptics : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    // --- Effect controls --------------------------------------------------

    /**
     * @brief Play a constant-force effect on the stick axes.
     *
     * @param slot       Independent effect slot (0–N); allows multiple
     *                   simultaneous constant forces.
     * @param strength   Normalised force level [-1.0, 1.0].  Negative values
     *                   reverse the direction along the X axis.
     * @param duration_ms Duration in milliseconds.  Pass SDL_HAPTIC_INFINITY
     *                   for a sustained effect.
     */
    int PlayConstant(int slot, float strength, uint32_t duration_ms) override;

    /**
     * @brief Play a periodic (sinusoidal) vibration effect.
     *
     * Useful for engine rumble, turbulence, weapons fire, etc.
     *
     * @param slot        Effect slot.
     * @param strength    Overall gain [0.0, 1.0].
     * @param period      Wave period in milliseconds.
     * @param magnitude   Peak amplitude [0.0, 1.0].
     * @param offset      DC offset [-1.0, 1.0].
     * @param phase       Phase in hundredths of a degree [0, 35999].
     * @param duration_ms Duration in milliseconds or SDL_HAPTIC_INFINITY.
     */
    int PlayPeriodic(int slot, float strength, uint32_t period,
                     float magnitude, float offset, uint32_t phase,
                     uint32_t duration_ms) override;

    /**
     * @brief Play a condition effect (spring, damper, inertia or friction).
     *
     * These are the most important effects for a flight stick: a spring
     * effect recreates stick centering force, damper adds drag, inertia
     * models mass, and friction adds a rough feel.
     *
     * @param slot         Effect slot.
     * @param type         Condition type (HapticConditionType::Spring, Damper, Inertia or Friction).
     * @param right_sat    Right-side saturation [0.0, 1.0].
     * @param left_sat     Left-side saturation [0.0, 1.0].
     * @param right_coeff  Right-side coefficient [-1.0, 1.0].
     * @param left_coeff   Left-side coefficient [-1.0, 1.0].
     * @param deadband     Dead-band around center [0.0, 1.0].
     * @param center       Effect center position [-1.0, 1.0].
     * @param duration_ms  Duration in milliseconds or SDL_HAPTIC_INFINITY.
     */
    int PlayCondition(int slot, HapticConditionType type,
                      float right_sat, float left_sat,
                      float right_coeff, float left_coeff,
                      float deadband, float center,
                      uint32_t duration_ms) override;

    /**
     * @brief Simulate rumble on devices that lack dedicated rumble motors.
     *
     * Implemented as a low-frequency periodic effect so that haptic-only
     * devices still feel something meaningful (e.g. cannon-fire impacts).
     */
    int PlayRumble(int slot, float large_magnitude, float small_magnitude,
                   uint32_t duration_ms) override;

    // --- Stop controls ----------------------------------------------------

    int StopConstant(int slot)  override;
    int StopPeriodic(int slot)  override;
    int StopCondition(int slot) override;

    void StopAll() override;
};