#pragma once
#include <string>
#include <SDL3/SDL.h>

class DeviceManager;
class PreferencesManager;

class InputMapper {
public:
    enum class OutputFormat {
        JSON,
        WebsocketWheel,
        OSC_Resonite
    };

    struct AxisConfig {
        int axisIndex = -1;
        bool invert = false;
        float deadzone = 0.05f;
        float minVal = -1.0f; // For remapping range
        float maxVal = 1.0f;
    };

    InputMapper(const DeviceManager& deviceManager);

    void DrawUI();
    std::string GenerateMessage();

    void LoadConfig(const PreferencesManager& prefs);
    void SaveConfig(PreferencesManager& prefs) const;

private:
    const DeviceManager& m_DeviceManager;
    SDL_JoystickID m_SelectedDeviceID = 0;
    OutputFormat m_OutputFormat = OutputFormat::JSON;
    
    AxisConfig m_Steering;
    AxisConfig m_Throttle;
    AxisConfig m_Brake;
    AxisConfig m_Clutch;
    AxisConfig m_Handbrake;

    float ProcessAxis(SDL_Joystick* joystick, const AxisConfig& config);
};
