#pragma once
#include "Devices/DeviceState.h"
#include "Devices/DeviceManager.h"

class GamepadHapticsVisualizer {
public:
    void Draw(const DeviceState& dev, DeviceManager& deviceManager);

private:
    // Rumble
    float m_low_freq = 0.5f;
    float m_high_freq = 0.5f;
    int m_duration = 1000;
    bool m_infinite_duration = false;

    // Trigger Rumble
    int m_left_trigger = 0;
    int m_right_trigger = 0;
    int m_trigger_duration = 1000;

    // DualSense
    int m_ds_effect_type = 0;
    int m_ds_params[5] = {0};
};
