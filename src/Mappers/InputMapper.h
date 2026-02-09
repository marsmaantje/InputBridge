#pragma once

#ifdef ENABLE_EXCLUSIVE_INPUT
#include "InputExclusiveMode.h"
#endif
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>

class DeviceManager;
class PreferencesManager;

class InputMapper {
  public:
    struct InputSource {
        std::string deviceGuid;
        SDL_JoystickID instance_id = 0; // Resolved at runtime
        int axisIndex = -1;
        bool invert = false;
        float deadzone = 0.05f;
        int outputRange = 0; // 0: -1..1, 1: 0..1, 2: -1..0
    };

    struct MappingProfile {
        std::string name;
        std::map<std::string, InputSource> outputToInput;
    };

    InputMapper(const DeviceManager &deviceManager);
    ~InputMapper();

    void DrawUI(PreferencesManager &prefs);
    std::string UpdateAndBroadcastMessage();

    void LoadConfig(PreferencesManager &prefs);
    void SaveConfig(PreferencesManager &prefs) const;
    void LoadProfiles();
    void SaveProfile(const MappingProfile &profile) const;
    void HandleDeviceConnectionChange();

  private:
    const DeviceManager &m_DeviceManager;
    std::vector<MappingProfile> m_Profiles;
    int m_SelectedProfileIndex = -1;
    char m_NewProfileName[128] = "";

#ifdef ENABLE_EXCLUSIVE_INPUT
    InputExclusiveMode m_ExclusiveModeHandler;
#endif

    const std::vector<std::string> m_GenericOutputs = {
        "Steering", "Throttle", "Brake", "Clutch", "Handbrake", "Pitch", "Roll"};

    float ProcessAxis(const InputSource &config);
#ifdef ENABLE_EXCLUSIVE_INPUT
    void ApplyExclusiveMode();
#endif
};
