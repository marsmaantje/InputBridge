#pragma once
#include "Devices/DeviceState.h"
#include "Devices/DeviceManager.h"

class SteeringWheelHapticsVisualizer {
public:
    void Draw(const DeviceState& dev, DeviceManager& deviceManager);

    // Draw only the RPM LED controls — independent of haptics availability.
    // Safe to call even when the device has no SDL haptic support.
    void DrawLEDs(DeviceManager& deviceManager);

private:
    // Constant
    float m_constant_strength = 0.5f;
    int m_constant_duration = 1000;
    bool m_constant_infinite_duration = false;

    // Periodic
    float m_periodic_strength = 1.0f;
    int m_periodic_period = 1000;
    float m_periodic_magnitude = 0.5f;
    float m_periodic_offset = 0.0f;
    int m_periodic_phase = 0;
    int m_periodic_duration = 1000;
    bool m_periodic_infinite_duration = false;

    // Condition
    int m_condition_slot = 0;
    int m_condition_type = 0; // 0: Spring, 1: Damper, 2: Inertia, 3: Friction
    float m_condition_right_sat = 1.0f;
    float m_condition_left_sat = 1.0f;
    float m_condition_right_coeff = 0.5f;
    float m_condition_left_coeff = 0.5f;
    float m_condition_deadband = 0.1f;
    float m_condition_center = 0.0f;
    int m_condition_duration = 5000;
    bool m_condition_infinite_duration = false;

    // RPM LEDs (wheel-rpm-lib)
    float m_rpm_percent = 0.0f;
};