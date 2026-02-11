#pragma once

#ifdef ENABLE_EXCLUSIVE_INPUT
#include "InputExclusiveMode.h"
#endif
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class DeviceManager;
class PreferencesManager;

// Moved from OutputMapper.h
struct HapticTarget {
    int virtual_id = 0;
    std::string name;
    std::string device_guid;
    SDL_JoystickID instance_id = 0;
    SDL_Haptic* haptic_device = nullptr;
    bool owns_haptic_device = true;

    // Effect Toggles
    bool enable_rumble = true;
    bool enable_constant = true;
    bool enable_periodic = true;
    bool enable_condition = true;

    // Cached Effect IDs
    int constant_effect_id = -1;
    int periodic_effect_id = -1;
    int condition_effect_id = -1;
    int rumble_effect_id = -1;

    std::string status_message;
};

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
        std::vector<HapticTarget> hapticTargets;
    };

    static InputMapper &GetInstance();
    static void Init(const DeviceManager &deviceManager);
    static void Shutdown();

    ~InputMapper();

    InputMapper(const InputMapper &) = delete;
    InputMapper &operator=(const InputMapper &) = delete;
    void DrawContent();
    std::string UpdateAndBroadcastMessage();

    void LoadConfig(PreferencesManager &prefs);
    void SaveConfig(PreferencesManager &prefs) const;
    void SaveCurrentProfile() const;
    void LoadProfiles();
    void SaveProfile(const MappingProfile &profile) const;
    void HandleDeviceConnectionChange();
    std::vector<HapticTarget>* GetCurrentHapticTargets();

  private:
    InputMapper(const DeviceManager &deviceManager);
    static std::unique_ptr<InputMapper> s_Instance;

    const DeviceManager &m_DeviceManager;
    std::vector<MappingProfile> m_Profiles;
    int m_SelectedProfileIndex = -1;
    char m_NewProfileName[128] = "";

    std::map<std::string, float> m_LastOutputValues;
    Uint64 m_LastBroadcastTime = 0;

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
