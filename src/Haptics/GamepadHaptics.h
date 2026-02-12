#pragma once

#include "HapticDevice.h"
#include <cstdint>
#include <string>
#include <map>

class GamepadHaptics : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    bool IsReady() const override;

    // Play the left/right rumble effect.
    // large_magnitude: 0-1 range for the large motor
    // small_magnitude: 0-1 range for the small motor
    // duration_ms: duration of the effect in milliseconds
    int Rumble(float large_magnitude, float small_magnitude, uint32_t duration_ms);

    // DualSense adaptive trigger effects
    // trigger: "left" or "right"
    // effect_type: "off", "feedback", "weapon", "vibration", "bow", "galloping", "machine"
    // params: effect-specific parameters
    int SendDualSenseTrigger(const char* trigger, const char* effect_type, const std::map<std::string, int>& params);

private:
    bool IsDualSense() const;
    bool IsDualSenseUSB() const;
    void SendDualSenseTriggerEffect(uint8_t* leftTriggerData, uint8_t* rightTriggerData);
};
