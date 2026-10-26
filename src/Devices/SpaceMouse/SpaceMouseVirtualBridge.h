// src/Devices/SpaceMouse/SpaceMouseVirtualBridge.h
//
// Exposes each SpaceMouseDevice as a virtual SDL_Joystick so InputMapper can
// map it with no changes - same approach as WiimoteVirtualBridge.
//
// Layout (append-only: MappingProfileStore persists bindings by raw index):
//   axes    0..5 = TX, TY, TZ, RX, RY, RZ (raw device convention, [-1,1])
//           6    = Battery (-1 = 0% or unknown, +1 = 100%)
//   buttons 0..N-1 = device buttons (N depends on the model)
#pragma once
#include "SpaceMouseDevice.h"
#include <SDL3/SDL.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace InputBridge::SpaceMouse {

enum SpaceMouseAxis {
    Axis_TX = 0, Axis_TY, Axis_TZ, Axis_RX, Axis_RY, Axis_RZ, Axis_Battery,
};
constexpr int kBridgeNumAxes = 7;

// Bridge joystick names look like "3Dconnexion SpaceNavigator (Mapped Inputs)".
constexpr const char *kBridgeNamePrefix = "3Dconnexion ";
constexpr const char *kBridgeNameSuffix = " (Mapped Inputs)";

// True for names produced by the bridge. Lets DeviceManager tell the bridge's
// own virtual joysticks apart from real ones, and InputLabelProvider pick the
// right label tables.
bool IsBridgeDeviceName(const std::string &name);

const char *BridgeAxisName(int axis); // nullptr out of range
// Label for a button on the bridge joystick called `device_name`.
std::string BridgeButtonName(const std::string &device_name, int button);

class SpaceMouseVirtualBridge {
public:
    static SpaceMouseVirtualBridge &GetInstance();

    SpaceMouseVirtualBridge(const SpaceMouseVirtualBridge &) = delete;
    SpaceMouseVirtualBridge &operator=(const SpaceMouseVirtualBridge &) = delete;

    // Attach bridge joysticks for new devices, detach for gone ones.
    void Sync(const std::vector<std::unique_ptr<SpaceMouseDevice>> &devices);
    // Write each device's current snapshot into its bridge joystick.
    void PushAllStates(const std::vector<std::unique_ptr<SpaceMouseDevice>> &devices);

    std::vector<SDL_JoystickID> GetAllJoystickIds() const;
    bool IsBridgeJoystick(SDL_JoystickID id) const;

    // Extra GUID component so two identical models stay distinguishable
    // (a HID-path hash, only when more than one of that model is attached).
    // Empty for the common single-device case, which keeps the GUID stable
    // across USB ports and replugs. nullopt if `id` isn't a bridge joystick.
    std::optional<std::string> GetGuidSuffix(SDL_JoystickID id) const;

private:
    SpaceMouseVirtualBridge() = default;

    struct Entry {
        std::string    hid_path;
        std::string    serial;
        std::string    joystick_name;
        SDL_JoystickID joystick_id = 0;
        SDL_Joystick  *joystick    = nullptr;
    };
    std::vector<Entry> m_Entries;

    Entry *Find(const std::string &hid_path);
    void   Attach(const SpaceMouseDevice &dev);
    void   Detach(Entry &entry);
};

} // namespace InputBridge::SpaceMouse
