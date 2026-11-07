#include "BatteryMonitor.h"
#include "App/Log.h"
#include "Devices/SpaceMouse/SpaceMouseVirtualBridge.h"
#include <cstdlib>

static constexpr const char* kTag = "BatteryMonitor";

namespace {

constexpr int kLogPercentDelta = 5;

struct PreviousBattery {
    SDL_PowerState state;
    int            percent;
};

const char* StateName(SDL_PowerState state) {
    switch (state) {
        case SDL_POWERSTATE_UNKNOWN:    return "UNKNOWN";
        case SDL_POWERSTATE_ON_BATTERY: return "ON_BATTERY";
        case SDL_POWERSTATE_NO_BATTERY: return "NO_BATTERY";
        case SDL_POWERSTATE_CHARGING:   return "CHARGING";
        case SDL_POWERSTATE_CHARGED:    return "CHARGED";
        default:                        return "INVALID";
    }
}

bool PercentChangedSignificantly(int oldPercent, int newPercent) {
    return oldPercent >= 0 && newPercent >= 0 &&
           std::abs(oldPercent - newPercent) >= kLogPercentDelta;
}

bool InfoChanged(const DeviceState& dev, const PreviousBattery& prev) {
    if (!dev.battery_initialized) return true;
    if (prev.state != dev.battery_state) return true;
    return PercentChangedSignificantly(prev.percent, dev.battery_percent);
}

void LogGamepad(const DeviceState& dev, const char* state) {
    if (dev.battery_state == SDL_POWERSTATE_UNKNOWN) {
        LOG_WARN(kTag, "Battery [%s]: State=%s (battery info not available)", dev.name.c_str(), state);
        LOG_WARN(kTag, "Possible causes: hid_playstation not loaded, missing udev rules, or SDL can't read battery");
    } else if (dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
        LOG_DEBUG(kTag, "Battery [%s]: State=%s, Percent=%d%%", dev.name.c_str(), state, dev.battery_percent);
    }
}

void LogJoystick(const DeviceState& dev, const char* state) {
    if (dev.battery_state == SDL_POWERSTATE_NO_BATTERY ||
        dev.battery_state == SDL_POWERSTATE_UNKNOWN) {
        return;
    }
    LOG_DEBUG(kTag, "Battery (Joystick) [%s]: State=%s, Percent=%d%%", dev.name.c_str(), state, dev.battery_percent);
}

void LogChange(DeviceState& dev, bool gamepad, const PreviousBattery& prev) {
    if (!InfoChanged(dev, prev)) return;
    dev.battery_initialized = true;
    const char* state = StateName(dev.battery_state);
    if (gamepad) LogGamepad(dev, state);
    else         LogJoystick(dev, state);
}

void ClearLeft(DeviceState& dev) {
    dev.battery_state_L = SDL_POWERSTATE_UNKNOWN;
    dev.battery_percent_L = -1;
}

// Queries power info of another (non-self) gamepad joystick. Returns false if
// the joystick isn't a candidate, can't be opened, or has no battery.
bool QueryOtherGamepad(const DeviceState& dev, SDL_JoystickID id, SDL_PowerState& state, int& percent) {
    if (id == dev.instance_id) return false;
    if (SDL_GetJoystickTypeForID(id) != SDL_JOYSTICK_TYPE_GAMEPAD) return false;
    SDL_Joystick* joystick = SDL_OpenJoystick(id);
    if (!joystick) return false;
    state = SDL_GetJoystickPowerInfo(joystick, &percent);
    SDL_CloseJoystick(joystick);
    return state != SDL_POWERSTATE_NO_BATTERY;
}

void UpdateLeft(DeviceState& dev) {
    ClearLeft(dev);
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        SDL_PowerState state = SDL_POWERSTATE_UNKNOWN;
        int percent = 0;
        if (!QueryOtherGamepad(dev, ids[i], state, percent)) continue;
        dev.battery_state_L = state;
        dev.battery_percent_L = percent;
        LOG_DEBUG(kTag, "Battery L [%s]: Percent=%d%%", dev.name.c_str(), percent);
        break;
    }
    SDL_free(ids);
}

void UpdateGamepad(DeviceState& dev, const PreviousBattery& prev) {
    int percent = 0;
    dev.battery_state = SDL_GetGamepadPowerInfo(dev.gamepad, &percent);
    dev.battery_percent = percent;
    LogChange(dev, true, prev);
    if (SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_L)) UpdateLeft(dev);
    else ClearLeft(dev);
}

void UpdateJoystick(DeviceState& dev, const PreviousBattery& prev) {
    int percent = 0;
    const auto bridgePercent =
        InputBridge::SpaceMouse::SpaceMouseVirtualBridge::GetInstance().GetBatteryPercent(dev.instance_id);
    if (bridgePercent.has_value()) {
        percent = *bridgePercent;
        dev.battery_state = percent >= 0 ? SDL_POWERSTATE_ON_BATTERY : SDL_POWERSTATE_UNKNOWN;
    } else {
        dev.battery_state = SDL_GetJoystickPowerInfo(dev.joystick, &percent);
    }
    dev.battery_percent = percent;
    LogChange(dev, false, prev);
}

} // namespace

void BatteryMonitor::UpdateDevice(DeviceState& dev) const {
    const PreviousBattery prev{dev.battery_state, dev.battery_percent};
    if (dev.gamepad) {
        UpdateGamepad(dev, prev);
    } else if (dev.joystick) {
        UpdateJoystick(dev, prev);
    } else {
        dev.battery_state = SDL_POWERSTATE_UNKNOWN;
        dev.battery_percent = -1;
    }
}

void BatteryMonitor::Update(std::vector<DeviceState>& devices, bool isMinimized) {
    // Battery levels change slowly; poll infrequently, and even less when minimized.
    const Uint64 now = SDL_GetTicks();
    const Uint64 interval = isMinimized ? m_IntervalMs * kMinimizedFactor : m_IntervalMs;
    if (now - m_LastUpdateMs <= interval) return;
    for (auto& dev : devices) UpdateDevice(dev);
    m_LastUpdateMs = now;
}
