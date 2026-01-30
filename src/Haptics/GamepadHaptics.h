#pragma once

#include "HapticDevice.h"

class GamepadHaptics : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    // Play the left/right rumble effect.
    // large_magnitude: 0-1 range for the large motor
    // small_magnitude: 0-1 range for the small motor
    // duration_ms: duration of the effect in milliseconds
    int PlayLeftRight(float large_magnitude, float small_magnitude, uint32_t duration_ms);
};
