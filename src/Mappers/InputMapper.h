#pragma once

// InputExclusiveMode / device-hide is managed by DeviceManager, not InputMapper.
#include "ButtonBinder.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class DeviceManager;
class PreferencesManager;
struct ProtocolDefinition;

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

        enum class Mode { Momentary, Toggle, SetOn, SetOff };
        Mode mode = Mode::Momentary;

        bool last_physical_state = false;
    };

    struct MappingProfile {
        std::string name;
        std::map<std::string, InputSource>   outputToInput;      // fieldId → axis source
        std::vector<HapticTarget>            hapticTargets;
        std::vector<ButtonToAnalogMapping>   buttonMappings;     // button → analog field
        std::vector<ButtonToDigitalMapping>  digitalMappings;    // button → digital field
        std::map<std::string, bool>          digitalToggleStates;

        // Protocol selections
        std::string oscOutputProtocolId;
        std::string oscInputProtocolId;
        std::string wsOutputProtocolId;
        std::string wsInputProtocolId;
        int selectedProtocolView = 0;

        // OSC server settings (per-profile)
        std::string oscSendHost  = "127.0.0.1";
        int         oscSendPort  = 9066;
        int         oscRecvPort  = 9068;
        bool        oscOutputEnabled = true;
        bool        oscInputEnabled  = true;

        // WebSocket server settings (per-profile)
        int  wsPort           = 4269;
        bool wsOutputEnabled  = true;
        bool wsInputEnabled   = true;
    };

    static InputMapper &GetInstance();
    static void Init(const DeviceManager &deviceManager);
    static void Shutdown();

    ~InputMapper();

    InputMapper(const InputMapper &) = delete;
    InputMapper &operator=(const InputMapper &) = delete;
    void DrawContent();
    void DrawProfileSelector();   // Draws only the profile selector bar (no Begin/End)
    void DrawMappingContent();           // Draws only the mapping content (no Begin/End)
    void DrawOutputProtocolSelector();   // Draws the output protocol combo for the active profile
    void DrawInputProtocolSelector();    // Draws the input protocol combo for the active profile (used by Output page)
    bool Update(bool dynamic_rate);
    std::string GetOutputPreview();

    void LoadConfig(PreferencesManager &prefs);
    void SaveConfig(PreferencesManager &prefs) const;
    void SaveCurrentProfile() const;
    void LoadProfiles();
    void SaveProfile(const MappingProfile &profile) const;
    void HandleDeviceConnectionChange();
    std::vector<HapticTarget>* GetCurrentHapticTargets();
    bool IsOutputAddressBound(const std::string& address) const;

    /**
     * @brief Atomically switch the active mapping profile.
     *
     * MappingProfile is a value type: all fields (axis mappings, haptic
     * targets, server settings, protocol IDs) are replaced wholesale on
     * activation.  This prevents partial-update bugs where, for example,
     * haptic targets reflect one profile while axis mappings reflect another.
     *
     * Always prefer this over directly writing m_SelectedProfileIndex so the
     * undo/redo system, the OutputMapper, and the server settings all stay in
     * sync with a single call.
     *
     * @param index Index into m_Profiles, or -1 to deactivate all profiles.
     */
    void ActivateProfile(int index);

    void CancelListening();

  private:
    InputMapper(const DeviceManager &deviceManager);
    static std::unique_ptr<InputMapper> s_Instance;

    const DeviceManager &m_DeviceManager;
    std::vector<MappingProfile> m_Profiles;
    int m_SelectedProfileIndex = -1;
    char m_NewProfileName[128] = "";
    char m_RenameProfileName[128] = "";

    int m_SelectedProtocolView = 0; // 0: OSC, 1: WebSocket

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
        std::map<SDL_JoystickID, std::vector<Uint8>> initialHatStates;
    };
    ListeningState m_ListeningState;

    // Legacy fallback outputs when no definition is selected
    const std::vector<std::string> m_GenericOutputs = {
        "Steering", "Throttle", "Brake", "Clutch", "Handbrake", "Pitch", "Roll"};

    // Helper for button binding
    class ButtonBinder m_buttonBinder;

    const ProtocolDefinition* GetActiveOutputDefinition();
    float ProcessAxis(const InputSource &config);
    void StartListening(ListeningState::Type type, const std::string& name, int index = -1);
    void UpdateListening();
    void UpdateActiveProtocols();
    void SnapshotServerSettings(MappingProfile& profile) const;
};