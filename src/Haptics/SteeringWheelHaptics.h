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
        float right_sat,
        float left_sat,
        float right_coeff,
        float left_coeff,
        float deadband,
        float center,
        uint32_t duration_ms
    );

private:
    int CreateAndRunEffect(SDL_HapticEffect& effect, uint32_t duration_ms);
    void UpdateEffect(int effect_id, SDL_HapticEffect& effect);
};
