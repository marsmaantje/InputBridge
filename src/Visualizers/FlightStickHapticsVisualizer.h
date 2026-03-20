#pragma once
#include "Devices/DeviceState.h"
#include "Devices/DeviceManager.h"

/**
 * @class FlightStickHapticsVisualizer
 * @brief ImGui panel for testing and monitoring haptic effects on flight sticks.
 *
 * Exposes controls for all four effect classes supported by FlightStickHaptics:
 *   - Constant Force  (sustained directional push)
 *   - Periodic / Sine (turbulence, engine vibration, …)
 *   - Condition       (spring centering, damper drag, inertia, friction)
 *   - Rumble          (impact feedback on devices without dedicated motors)
 *
 * The "Active Haptic Slots" child window mirrors the one in
 * SteeringWheelHapticsVisualizer so users have a consistent mental model.
 */
class FlightStickHapticsVisualizer {
public:
    void Draw(const DeviceState& dev, DeviceManager& deviceManager);

private:
    // --- Constant Force ---
    int   m_constant_slot              = 0;
    float m_constant_strength          = 0.5f;
    int   m_constant_duration          = 1000;
    bool  m_constant_infinite_duration = false;

    // --- Periodic (Sine) ---
    int   m_periodic_slot              = 0;
    int   m_periodic_wave_type         = 0;  // 0=Sine, 1=Triangle, 2=SawtoothUp, 3=SawtoothDown
    float m_periodic_strength          = 1.0f;
    int   m_periodic_period            = 500;
    float m_periodic_magnitude         = 0.5f;
    float m_periodic_offset            = 0.0f;
    int   m_periodic_phase             = 0;
    int   m_periodic_duration          = 1000;
    bool  m_periodic_infinite_duration = false;

    // --- Condition Effects ---
    int   m_condition_slot              = 0;
    int   m_condition_type              = 0;  // 0: Spring, 1: Damper, 2: Inertia, 3: Friction
    float m_condition_right_sat         = 1.0f;
    float m_condition_left_sat          = 1.0f;
    float m_condition_right_coeff       = 0.5f;
    float m_condition_left_coeff        = 0.5f;
    float m_condition_deadband          = 0.1f;
    float m_condition_center            = 0.0f;
    int   m_condition_duration          = 5000;
    bool  m_condition_infinite_duration = false;

    // --- Rumble ---
    int   m_rumble_slot              = 0;
    float m_rumble_large             = 0.5f;
    float m_rumble_small             = 0.3f;
    int   m_rumble_duration          = 500;
    bool  m_rumble_infinite_duration = false;
};