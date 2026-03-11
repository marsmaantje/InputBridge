#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// Preset device types offered in the "Add Virtual Device" UI.
// Each maps to an SDL joystick type so DeviceManager / DeviceFactory route it
// correctly (e.g. WHEEL → SteeringWheelHaptics path).
// ─────────────────────────────────────────────────────────────────────────────
enum class VirtualDeviceType {
    Gamepad,        // SDL_JOYSTICK_TYPE_GAMEPAD
    SteeringWheel,  // SDL_JOYSTICK_TYPE_WHEEL
    FlightStick,    // SDL_JOYSTICK_TYPE_FLIGHT_STICK
    Generic,        // SDL_JOYSTICK_TYPE_UNKNOWN
};

static inline const char* VirtualDeviceTypeName(VirtualDeviceType t) {
    switch (t) {
        case VirtualDeviceType::Gamepad:       return "Gamepad";
        case VirtualDeviceType::SteeringWheel: return "Steering Wheel";
        case VirtualDeviceType::FlightStick:   return "Flight Stick";
        default:                               return "Generic";
    }
}

// Per-axis metadata used by the visualizer to show sensible labels and ranges.
struct VirtualAxisInfo {
    const char* label;
    float       defaultValue; // 0.0 for most axes, -1.0 for triggers that rest low
};

// ─────────────────────────────────────────────────────────────────────────────
// Holds the writable state for one virtual SDL joystick.
// ─────────────────────────────────────────────────────────────────────────────
struct VirtualDeviceState {
    SDL_JoystickID joystick_id = 0;
    SDL_Joystick*  joystick    = nullptr;

    std::string       name;
    VirtualDeviceType type = VirtualDeviceType::Generic;

    // Normalized axis values in [-1, 1].  Written by the UI, pushed to SDL each
    // frame via SDL_SetJoystickVirtualAxis.
    std::vector<float> axes;

    // Button pressed states.
    std::vector<bool> buttons;

    // Single hat (D-pad); SDL_HAT_* constants.
    Uint8 hat = SDL_HAT_CENTERED;

    // Per-axis metadata (size == axes.size()).
    std::vector<VirtualAxisInfo> axisInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// Manages the lifetime and state of all virtual SDL joysticks.
//
// Usage:
//   auto id = VirtualDeviceManager::GetInstance().AddDevice(
//                 VirtualDeviceType::Gamepad, "Test Pad");
//   // SDL fires SDL_EVENT_JOYSTICK_ADDED → DeviceManager::HandleDeviceAdded()
//   // Existing visualisers / InputMapper work with no changes.
//   ...
//   VirtualDeviceManager::GetInstance().PushState(id);   // each frame
// ─────────────────────────────────────────────────────────────────────────────
class VirtualDeviceManager {
public:
    static VirtualDeviceManager& GetInstance();

    // Attach a new virtual joystick.  SDL fires SDL_EVENT_JOYSTICK_ADDED.
    // Returns 0 on failure.
    SDL_JoystickID AddDevice(VirtualDeviceType type, const std::string& name);

    // Detach a virtual joystick.  SDL fires SDL_EVENT_JOYSTICK_REMOVED.
    void RemoveDevice(SDL_JoystickID id);

    // Push current axis / button / hat state to SDL so InputMapper reads them.
    // Call once per frame for every virtual device.
    void PushState(SDL_JoystickID id);
    void PushAllStates();

    // Returns nullptr if not found.
    VirtualDeviceState* GetState(SDL_JoystickID id);

    const std::vector<std::unique_ptr<VirtualDeviceState>>& GetDevices() const;

private:
    VirtualDeviceManager() = default;
    VirtualDeviceManager(const VirtualDeviceManager&) = delete;
    VirtualDeviceManager& operator=(const VirtualDeviceManager&) = delete;

    std::vector<std::unique_ptr<VirtualDeviceState>> m_Devices;

    // Builds a VirtualDeviceState with the correct axis count / labels for
    // the given preset type.
    static std::unique_ptr<VirtualDeviceState> MakeState(
        VirtualDeviceType type, const std::string& name,
        SDL_JoystickID id, SDL_Joystick* joystick);
};
