#pragma once

#ifdef ENABLE_EXCLUSIVE_INPUT
#include "InputExclusiveMode.h"
#endif
#include <SDL3/SDL.h>
#include <string>

class DeviceManager;
class PreferencesManager;

class InputMapper {
  public:
    struct AxisConfig {
        int axisIndex = -1;
        bool invert = false;
        float deadzone = 0.05f;
        int outputRange = 0; // 0: -1..1, 1: 0..1, 2: -1..0
    };

    InputMapper(const DeviceManager &deviceManager);
    ~InputMapper();

    void DrawUI();
    std::string UpdateAndBroadcastMessage();

    void LoadConfig(const PreferencesManager &prefs);
    void SaveConfig(PreferencesManager &prefs) const;

  private:
    const DeviceManager &m_DeviceManager;
    SDL_JoystickID m_SelectedDeviceID = 0;

#ifdef ENABLE_EXCLUSIVE_INPUT
    InputExclusiveMode m_ExclusiveModeHandler;
#endif

    AxisConfig m_Steering;
    AxisConfig m_Throttle;
    AxisConfig m_Brake;
    AxisConfig m_Clutch;
    AxisConfig m_Handbrake;
    AxisConfig m_Pitch;
    AxisConfig m_Roll;

    float ProcessAxis(SDL_Joystick *joystick, const AxisConfig &config);
#ifdef ENABLE_EXCLUSIVE_INPUT
    void ApplyExclusiveMode();
#endif
};