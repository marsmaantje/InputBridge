#pragma once
#include "Haptics/HapticDevice.h"

class SteeringWheelHaptics : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    /** Steering wheels support full force-feedback and gain control. */
    HapticCapabilities caps() const override {
        HapticCapabilities c;
        c.forceFeedback = true;
        c.gainControl   = true;
        return c;
    }

    int SetGain(int gain);

    // All effects accept a slot, consistent with PlayCondition.
    int PlayConstant(int slot, float strength, uint32_t duration_ms) override;
    int PlayPeriodic(int slot, HapticPeriodicType wave_type, float strength, uint32_t period, float magnitude, float offset, uint32_t phase, uint32_t duration_ms) override;
    int PlayCondition(int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, uint32_t duration_ms) override;

    int StopConstant(int slot) override;
    int StopPeriodic(int slot) override;
    int StopCondition(int slot) override;

    // PlayRumble on a steering wheel is simulated via a low-frequency periodic.
    // It reuses PlayPeriodic internally and does not need its own slot map.
    int PlayRumble(int slot, float large_magnitude, float small_magnitude, uint32_t duration_ms) override;

    void StopAll() override;
};