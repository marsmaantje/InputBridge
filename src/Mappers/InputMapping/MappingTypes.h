#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Pure data types describing a mapping profile: how device axes/buttons/
// sensors map onto protocol output fields.
// ─────────────────────────────────────────────────────────────────────────────

#include <SDL3/SDL.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

// HapticTarget describes a virtual-device-id → physical-haptic-device binding.
// It stays at global scope (not in InputMapping) because OutputMapper.h
// forward-declares and uses it as a bare global type — moving it into a
// namespace would be a breaking change for that consumer.
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

namespace InputMapping {

// Maps a device axis to a named analog output channel (field id or legacy name)
struct InputSource {
    std::string deviceGuid;
    SDL_JoystickID instance_id = 0;
    int axisIndex = -1;
    bool invert = false;
    float deadzone = 0.05f;
    int outputRange = 0; // 0: -1..1, 1: 0..1, 2: -1..0

    // ── Sensor source ─────────────────────────────────────────────────
    // When sensorChannel is not None, this source reads from the device
    // sensor/touchpad instead of a regular joystick axis (axisIndex is
    // unused in that case).
    enum class SensorChannel {
        None,
        GyroX, GyroY, GyroZ,
        AccelX, AccelY, AccelZ,
        TouchX, TouchY, TouchPressure,
        Touch2X, Touch2Y, Touch2Pressure,
        LeftStickTouch, RightStickTouch,
        LeftGripTouch, RightGripTouch,
        GyroLX, GyroLY, GyroLZ,
        AccelLX, AccelLY, AccelLZ,
        GyroRX, GyroRY, GyroRZ,
        AccelRX, AccelRY, AccelRZ,
        BatteryLevel,
        BatteryCharging
    };
    SensorChannel sensorChannel = SensorChannel::None;
};

// Maps a device button to an analog output channel (on/off float values)
struct ButtonToAnalogMapping {
    std::string device_guid;
    SDL_JoystickID instance_id = 0;
    int button_index = -1;
    int hat_index = -1;
    int hat_mask = 0;
    InputSource::SensorChannel sensor_channel = InputSource::SensorChannel::None;
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
    InputSource::SensorChannel sensor_channel = InputSource::SensorChannel::None;
    std::string target_field_id; // FieldDescriptor::id

    enum class Mode { Momentary, Toggle, SetOn, SetOff };
    Mode mode = Mode::Momentary;

    bool last_physical_state = false;
};

// Maps a device analog axis (or sensor) to a digital output field via a threshold
struct AnalogToDigitalMapping {
    InputSource source;              // reuse InputSource for axis/sensor + options
    std::string target_field_id;     // FieldDescriptor::id  (digital field)
    float threshold = 0.5f;          // crossing point in the source's output range
    bool  invert_threshold = false;  // true → active when value < threshold

    bool last_state = false;         // for Toggle/SetOn/SetOff edge detection
    enum class Mode { Momentary, Toggle, SetOn, SetOff };
    Mode mode = Mode::Momentary;
};

// Mixes multiple analog sources into a single analog output field.
// Each source has its own InputSource (device/axis/sensor) and a weight.
// Mixed value = sum(ProcessAxis(src) * weight), optionally clamped to [-1, 1].
struct ChannelMix {
    struct MixSource {
        InputSource source;
        float weight = 1.0f;
    };
    std::string              target_field_id;   // FieldDescriptor::id (analog field)
    std::vector<MixSource>   sources;
    bool                     clamp_output = true;
};

struct MappingProfile {
    std::string name;
    std::map<std::string, InputSource>      outputToInput;            // fieldId → axis source
    std::vector<::HapticTarget>             hapticTargets;
    std::vector<ButtonToAnalogMapping>      buttonMappings;           // button → analog field
    std::vector<ButtonToDigitalMapping>     digitalMappings;          // button → digital field
    std::vector<AnalogToDigitalMapping>     analogToDigitalMappings;  // axis → digital field
    std::vector<ChannelMix>                 channelMixes;             // mixed sources → analog field
    std::map<std::string, bool>             digitalToggleStates;

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

} // namespace InputMapping

// Legacy fallback output names used when no protocol definition is selected.
// Shared by the UI (legacy mapping table) and the runtime updater
inline const std::vector<std::string> kGenericOutputs = {
    "Steering", "Throttle", "Brake", "Clutch", "Handbrake", "Pitch", "Roll"};
