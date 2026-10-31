// src/Devices/SpaceMouse/SpaceMouseVirtualBridge.cpp
#include "SpaceMouseVirtualBridge.h"
#include "App/Log.h"
#include <algorithm>
#include <functional>
#include <iterator>
#include <string_view>

namespace InputBridge::SpaceMouse {

namespace { constexpr const char *kTag = "SpaceMouseVirtualBridge"; }

bool IsBridgeDeviceName(std::string_view name) {
    const std::string p = kBridgeNamePrefix, s = kBridgeNameSuffix;
    return name.size() > p.size() + s.size() &&
           name.compare(0, p.size(), p) == 0 &&
           name.compare(name.size() - s.size(), s.size(), s) == 0;
}

const char *BridgeAxisName(int axis) {
    switch (axis) {
        case Axis_TX:      return "Translation X";
        case Axis_TY:      return "Translation Y";
        case Axis_TZ:      return "Translation Z";
        case Axis_RX:      return "Rotation X (Pitch)";
        case Axis_RY:      return "Rotation Y (Roll)";
        case Axis_RZ:      return "Rotation Z (Yaw)";
        case Axis_Battery: return "Battery Level";
        default:           return nullptr;
    }
}

std::string BridgeButtonName(const std::string &device_name, int button) {
    if (IsBridgeDeviceName(device_name)) {
        const std::string p = kBridgeNamePrefix, s = kBridgeNameSuffix;
        const std::string model = device_name.substr(p.size(), device_name.size() - p.size() - s.size());
        if (const ModelInfo *m = FindModelByName(model)) return ButtonName(*m, button);
    }
    return "Button " + std::to_string(button);
}

SpaceMouseVirtualBridge &SpaceMouseVirtualBridge::GetInstance() {
    static SpaceMouseVirtualBridge instance;
    return instance;
}

SpaceMouseVirtualBridge::Entry *SpaceMouseVirtualBridge::Find(std::string_view hid_path) {
    for (auto &e : m_Entries)
        if (e.hid_path == hid_path) return &e;
    return nullptr;
}

void SpaceMouseVirtualBridge::Attach(const SpaceMouseDevice &dev) {
    const auto &snap = dev.Snapshot();
    const std::string name = std::string(kBridgeNamePrefix) + snap.model.name + kBridgeNameSuffix;

    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    // Not GAMEPAD-typed: that would route through SDL_OpenGamepad and the
    // default gamepad mapping would mislabel these axes.
    desc.type     = static_cast<Uint16>(SDL_JOYSTICK_TYPE_UNKNOWN);
    desc.naxes    = static_cast<Uint16>(kBridgeNumAxes);
    desc.nbuttons = static_cast<Uint16>(std::clamp(snap.model.buttons, 0, kMaxButtons));
    desc.nhats    = 0;
    desc.name     = name.c_str();

    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        LOG_ERROR(kTag, "SDL_AttachVirtualJoystick failed for %s: %s", snap.hid_path.c_str(), SDL_GetError());
        return;
    }
    SDL_Joystick *joystick = SDL_OpenJoystick(id);
    if (!joystick) {
        LOG_ERROR(kTag, "SDL_OpenJoystick failed for bridge joystick (%s): %s", snap.hid_path.c_str(), SDL_GetError());
        SDL_DetachVirtualJoystick(id);
        return;
    }
    Entry e;
    e.hid_path = snap.hid_path;
    e.serial = snap.serial;
    e.joystick_name = name;
    e.joystick_id = id;
    e.joystick = joystick;
    m_Entries.push_back(e);
    LOG_INFO(kTag, "Bridged '%s' -> virtual joystick id=%u (%s)", snap.hid_path.c_str(),
             static_cast<unsigned>(id), name.c_str());
}

void SpaceMouseVirtualBridge::Detach(const Entry &entry) const {
    // SDL_EVENT_JOYSTICK_REMOVED makes DeviceManager close the handle; don't
    // close it here too (same rule as WiimoteVirtualBridge::Detach).
    SDL_DetachVirtualJoystick(entry.joystick_id);
}

void SpaceMouseVirtualBridge::Sync(const std::vector<std::unique_ptr<SpaceMouseDevice>> &devices) {
    for (auto &dev : devices)
        if (!Find(dev->Snapshot().hid_path)) Attach(*dev);

    auto present = [&](const std::string &path) {
        return std::ranges::any_of(devices,
                       [&](const auto &d) { return d->Snapshot().hid_path == path; });
    };
    auto removed = std::ranges::remove_if(m_Entries, [&](Entry &e) {
        if (present(e.hid_path)) return false;
        Detach(e);
        return true;
    });
    if (removed.begin() != removed.end()) {
        LOG_INFO(kTag, "Removing %td bridge joystick(s) for disconnected 3D mice",
                 std::distance(removed.begin(), removed.end()));
    }
    m_Entries.erase(removed.begin(), removed.end());
}

void SpaceMouseVirtualBridge::PushAllStates(const std::vector<std::unique_ptr<SpaceMouseDevice>> &devices) {
    for (auto &dev : devices) {
        Entry *e = Find(dev->Snapshot().hid_path);
        if (!e || !e->joystick) continue;
        const auto &snap = dev->Snapshot();

        for (int i = 0; i < kNumMotionAxes; ++i) {
            const float v = NormalizeAxis(snap.state.raw[i]);
            SDL_SetJoystickVirtualAxis(e->joystick, i, static_cast<Sint16>(v * 32767.0f));
        }
        // 0..100 % -> -1..+1, same "rest = -1" convention as the other bridges.
        const float bat = snap.state.battery_percent < 0
            ? -1.f : (snap.state.battery_percent / 100.f) * 2.f - 1.f;
        SDL_SetJoystickVirtualAxis(e->joystick, Axis_Battery, static_cast<Sint16>(bat * 32767.0f));
        e->battery_percent = snap.state.battery_percent;

        const int nbuttons = std::clamp(snap.model.buttons, 0, kMaxButtons);
        for (int b = 0; b < nbuttons; ++b)
            SDL_SetJoystickVirtualButton(e->joystick, b, (snap.state.buttons >> b) & 1u);
    }
}

std::vector<SDL_JoystickID> SpaceMouseVirtualBridge::GetAllJoystickIds() const {
    std::vector<SDL_JoystickID> ids;
    for (const auto &e : m_Entries) ids.push_back(e.joystick_id);
    return ids;
}

bool SpaceMouseVirtualBridge::IsBridgeJoystick(SDL_JoystickID id) const {
    return std::ranges::any_of(m_Entries, [&](const Entry &e) { return e.joystick_id == id; });
}

std::optional<int> SpaceMouseVirtualBridge::GetBatteryPercent(SDL_JoystickID id) const {
    for (const auto &e : m_Entries)
        if (e.joystick_id == id) return e.battery_percent;
    return std::nullopt;
}

std::optional<std::string> SpaceMouseVirtualBridge::GetGuidSuffix(SDL_JoystickID id) const {
    const Entry *self = nullptr;
    for (const auto &e : m_Entries) if (e.joystick_id == id) { self = &e; break; }
    if (!self) return std::nullopt;

    const auto twins = std::ranges::count_if(m_Entries,
        [&](const Entry &e) { return e.joystick_name == self->joystick_name; });
    
    if (twins > 1) {
        return std::to_string(std::hash<std::string>{}(self->hid_path));
    }
    return std::string(); // single device: plain, port-independent GUID
}

} // namespace InputBridge::SpaceMouse
