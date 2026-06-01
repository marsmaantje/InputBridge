#pragma once
#include "DeviceVisualizer.h"

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
    void DrawGyro     (SDL_Gamepad* gamepad);
    void DrawAccel    (SDL_Gamepad* gamepad);
    void DrawGyroL    (SDL_Gamepad* gamepad);
    void DrawAccelL   (SDL_Gamepad* gamepad);
    void DrawGyroR    (SDL_Gamepad* gamepad);
    void DrawAccelR   (SDL_Gamepad* gamepad);
    void DrawTouch    (SDL_Gamepad* gamepad);
    void DrawCapSense (SDL_Gamepad* gamepad);

    /// Horizontal bar showing a [-1, 1] value with axis label.
    static void DrawAxisBar(const char* label, float value, float width = 220.f);

    /// Lit/unlit pill-shaped indicator for a boolean capacitive-touch input.
    static void DrawCapSenseButton(const char* label, bool active);
};