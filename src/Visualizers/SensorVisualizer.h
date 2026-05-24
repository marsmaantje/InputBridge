#pragma once
#include "DeviceVisualizer.h"
#include "Devices/SensorState.h"

/**
 * @file SensorVisualizer.h
 * @brief ImGui visualiser tab for gyro, accelerometer, and touchpad data.
 *
 * Shown as the "Sensors" tab in DevicePanel for DualSense and Steam Controllers.
 * Displays live values with bar graphs and a 2D touchpad surface.
 * Also shows the current raw → normalised mapping so the user understands
 * what each axis means when they set up sensor mappings in the Input Mapper.
 */
class SensorVisualizer : public DeviceVisualizer {
public:
    void Draw(const DeviceState& dev) override;

private:
    void DrawGyro (SDL_Gamepad* gamepad);
    void DrawAccel(SDL_Gamepad* gamepad);
    void DrawTouch(SDL_Gamepad* gamepad);

    /// Horizontal bar showing a [-1, 1] value with axis label.
    static void DrawAxisBar(const char* label, float value, float width = 220.f);
};
