#pragma once

#include "HapticDevice.h"

class SteeringWheelHaptics : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    int SetGain(int gain);

    int PlayConstant(float strength, uint32_t duration_ms);

    int PlayPeriodic(
        float strength,
        uint32_t period,
        float magnitude,
        float offset,
        uint32_t phase,
        uint32_t duration_ms);

    int PlayCondition(
        int slot,
        uint16_t type,
        float right_sat, float left_sat,
        float right_coeff, float left_coeff,
        float deadband, float center,
        uint32_t duration_ms
    );

    int StopCondition(int slot);
};
