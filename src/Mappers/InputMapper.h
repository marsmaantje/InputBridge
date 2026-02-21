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
    // Maps a device axis to a named analog output channel (field id or legacy name)
    struct InputSource {
        std::string deviceGuid;
        SDL_JoystickID instance_id = 0;
        int axisIndex = -1;
        bool invert = false;
        float deadzone = 0.05f;
        int outputRange = 0; // 0: -1..1, 1: 0..1, 2: -1..0
    };

    // Maps a device button to an analog output channel (on/off float values)
    struct ButtonToAnalogMapping {
        std::string device_guid;
        SDL_JoystickID instance_id = 0;
        int button_index = 0;
        std::string target_output_name; // field id or legacy name
        float on_value = 1.0f;
        float off_value = 0.0f;
    };

    // Maps a device button to a digital output channel (field id from definition)
    struct ButtonToDigitalMapping {
        std::string device_guid;
        SDL_JoystickID instance_id = 0;
        int button_index = -1;
        int hat_index = -1;
        int hat_mask = 0;
        std::string target_field_id; // FieldDescriptor::id
    };

    struct MappingProfile {
        std::string name;
        std::map<std::string, InputSource>   outputToInput;      // fieldId → axis source
        std::vector<HapticTarget>            hapticTargets;
        std::vector<ButtonToAnalogMapping>   buttonMappings;     // button → analog field
        std::vector<ButtonToDigitalMapping>  digitalMappings;    // button → digital field
    };

    static InputMapper &GetInstance();
    static void Init(const DeviceManager &deviceManager);
    static void Shutdown();

    ~InputMapper();

    InputMapper(const InputMapper &) = delete;
    InputMapper &operator=(const InputMapper &) = delete;
    void DrawContent();
    bool Update(bool dynamic_rate);
    std::string GetOutputPreview();

    void LoadConfig(PreferencesManager &prefs);
    void SaveConfig(PreferencesManager &prefs) const;
    void SaveCurrentProfile() const;
    void LoadProfiles();
    void SaveProfile(const MappingProfile &profile) const;
    void HandleDeviceConnectionChange();
    std::vector<HapticTarget>* GetCurrentHapticTargets();
    
    void CancelListening();

  private:
    InputMapper(const DeviceManager &deviceManager);
    static std::unique_ptr<InputMapper> s_Instance;

    const DeviceManager &m_DeviceManager;
    std::vector<MappingProfile> m_Profiles;
    int m_SelectedProfileIndex = -1;
    char m_NewProfileName[128] = "";
    char m_RenameProfileName[128] = "";

    std::map<std::string, float> m_LastOutputValues;
    Uint64 m_LastBroadcastTime = 0;

    struct ListeningState {
        bool active = false;
        enum Type { None, Axis, Digital } type = None;
        std::string targetName; // Field ID for axes, or category for lists
        int listIndex = -1;     // Index in the list (for digital/button mappings)

        struct AxisState {
            SDL_JoystickID instance_id;
            int axis_index;
            Sint16 value;
        };
        std::vector<AxisState> initialAxes;
    };
    ListeningState m_ListeningState;

#ifdef ENABLE_EXCLUSIVE_INPUT
    InputExclusiveMode m_ExclusiveModeHandler;
#endif

    // Legacy fallback outputs when no definition is selected
    const std::vector<std::string> m_GenericOutputs = {
        "Steering", "Throttle", "Brake", "Clutch", "Handbrake", "Pitch", "Roll"};

    float ProcessAxis(const InputSource &config);
    void StartListening(ListeningState::Type type, const std::string& name, int index = -1);
    void UpdateListening();
#ifdef ENABLE_EXCLUSIVE_INPUT
    void ApplyExclusiveMode();
#endif
};
