#include "App/Log.h"
#include "DeviceManager.h"
#include "DeviceFactory.h"
#include "SensorReader.h"
#include "SDL3/SDL_joystick.h"
#include <algorithm>
#include <cstdlib>

static constexpr const char* kTag = "DeviceManager";

DeviceManager &DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

DeviceManager::DeviceManager() {
}

DeviceManager::~DeviceManager() { CloseAllDevices(); }

const std::vector<DeviceState> &DeviceManager::GetDevices() const { return m_Devices; }
std::vector<DeviceState>       &DeviceManager::GetDevices()       { return m_Devices; }

std::string DeviceManager::GetDeviceGUIDString(const DeviceState &dev) {
    SDL_Joystick* joystick = SDL_GetJoystickFromID(dev.instance_id);
    if (!joystick) {
        return "00000000000000000000000000000000";
    }
    SDL_GUID guid = SDL_GetJoystickGUID(joystick);
    char guidStr[33];
    SDL_GUIDToString(guid, guidStr, sizeof(guidStr));
    return std::string(guidStr);
}

void DeviceManager::HandleDeviceAdded(SDL_JoystickID instance_id) {
    auto result = InputBridge::DeviceFactory::CreateDevice(instance_id);
    if (!result) {
        LOG_ERROR(kTag, "Failed to create device %d", instance_id);
        return;
    }
    
    m_Devices.push_back(std::move(result->state));
    
    // Initialize battery info for the new device
    UpdateBatteryInfo(m_Devices.back());
    
    // ── Enable all IMU sensors at connect-time ──────────────────────────────
    // SDL3 requires sensors to be enabled before the first read. For a combined
    // Joy-Con pair, SDL_SENSOR_GYRO_L and SDL_SENSOR_ACCEL_L (left Joy-Con)
    // are separate streams from SDL_SENSOR_GYRO_R / SDL_SENSOR_ACCEL_R (right).
    // Without an explicit enable here, ReadGyroL / ReadAccelL always return
    // available=false because SDL never starts the left-side sensor pipeline.
    if (m_Devices.back().gamepad)
        SensorReader::EnableAll(m_Devices.back().gamepad);
    // ── End sensor enable ─────────────────────────────────────────────────── 

    if (result->haptic) {
        m_HapticDevices[instance_id] = std::move(result->haptic);
    }

    // When a steering wheel is connected, re-scan for RPM-capable devices via
    // wheel-rpm-lib so the visualizer can immediately offer LED control.
    if (SDL_GetJoystickTypeForID(instance_id) == SDL_JOYSTICK_TYPE_WHEEL) {
        ScanWheelRPMDevices();
    }
}

void DeviceManager::HandleDeviceRemoved(SDL_JoystickID instance_id) {
    // CRITICAL FIX: Manually close the haptic device before erasing
    // This ensures SDL_CloseHaptic is called before SDL_CloseGamepad/Joystick
    // which prevents double-free when the gamepad close also closes the haptic
    auto haptic_it = m_HapticDevices.find(instance_id);
    if (haptic_it != m_HapticDevices.end()) {
        // Manually call Close() to clean up haptic before SDL closes it
        if (haptic_it->second) {
            haptic_it->second->Close();
        }
        // Now erase (destructor will be called but Close() is idempotent)
        m_HapticDevices.erase(haptic_it);
    }
    
    auto it = std::remove_if(m_Devices.begin(), m_Devices.end(), [instance_id](const DeviceState &dev) {
        if (dev.instance_id == instance_id) {
            if (dev.gamepad)
                SDL_CloseGamepad(dev.gamepad);
            else if (dev.joystick)
                SDL_CloseJoystick(dev.joystick);
            return true;
        }
        return false;
    });

    if (it != m_Devices.end()) {
        m_Devices.erase(it, m_Devices.end());
    }

    // If no steering wheels remain, clear the RPM device list.
    bool anyWheelLeft = false;
    for (const auto& dev : m_Devices) {
        if (SDL_GetJoystickTypeForID(dev.instance_id) == SDL_JOYSTICK_TYPE_WHEEL) {
            anyWheelLeft = true;
            break;
        }
    }
    if (!anyWheelLeft) {
        m_WheelRPMDevices.clear();
    }
}

void DeviceManager::CloseAllDevices() {
    // Close haptic devices first (before their joysticks are closed)
    for (auto& pair : m_HapticDevices) {
        if (pair.second) {
            pair.second->Close();
        }
    }
    m_HapticDevices.clear();
    
    // Release wheel RPM devices
    m_WheelRPMDevices.clear();

    // Now close SDL devices
    for (auto &dev : m_Devices) {
        if (dev.gamepad)
            SDL_CloseGamepad(dev.gamepad);
        else if (dev.joystick)
            SDL_CloseJoystick(dev.joystick);
    }
    m_Devices.clear();
}

void DeviceManager::Update(bool isMinimized) {
    // Refresh battery info every 5 seconds when active,
    // or every 30 seconds when minimized.
    // Controller battery levels change very slowly, so high-frequency
    // polling is unnecessary and CPU-intensive on some platforms.
    static Uint64 lastBatteryUpdate = 0;
    Uint64 now = SDL_GetTicks();

    // If minimized, we multiply the interval by 6 (e.g. 5s -> 30s)
    // to further reduce background overhead.
    Uint64 interval = isMinimized ? (m_BatteryUpdateIntervalMs * 6) : m_BatteryUpdateIntervalMs;

    if (now - lastBatteryUpdate > interval) {
        for (auto &dev : m_Devices) {
            UpdateBatteryInfo(dev);
        }
        lastBatteryUpdate = now;
    }
}

// ---------------------------------------------------------------------------
// wheel-rpm-lib integration
// ---------------------------------------------------------------------------

void DeviceManager::ScanWheelRPMDevices() {
    m_WheelRPMDevices = wheel::WheelManager::scan();
    LOG_INFO(kTag, "WheelRPM scan complete: %zu device(s) found",
            m_WheelRPMDevices.size());
}

const std::vector<std::unique_ptr<wheel::Wheel>>&
DeviceManager::GetWheelRPMDevices() const {
    return m_WheelRPMDevices;
}

HapticDevice *DeviceManager::GetHapticDevice(SDL_JoystickID instance_id) const {
    auto it = m_HapticDevices.find(instance_id);
    if (it != m_HapticDevices.end()) {
        return it->second.get();
    }
    return nullptr;
}

void DeviceManager::SetDeviceKeepalive(SDL_JoystickID instance_id, bool enable) {
    HapticDevice* haptic = GetHapticDevice(instance_id);
    if (haptic) haptic->EnableKeepalive(enable);
}

void DeviceManager::UpdateBatteryInfo(DeviceState &dev) {
    SDL_PowerState old_state = dev.battery_state;
    int old_percent = dev.battery_percent;

    if (dev.gamepad) {
        int percent = 0;
        dev.battery_state = SDL_GetGamepadPowerInfo(dev.gamepad, &percent);
        dev.battery_percent = percent;

        bool state_changed   = (old_state != dev.battery_state);
        // Only compare percents when both readings are valid numbers.
        bool percent_changed = (dev.battery_percent >= 0 && old_percent >= 0 &&
                                abs(old_percent - dev.battery_percent) >= 5);

        if (!dev.battery_initialized || state_changed || percent_changed) {
            dev.battery_initialized = true;

            const char* state_str;
            switch (dev.battery_state) {
                case SDL_POWERSTATE_UNKNOWN:    state_str = "UNKNOWN";    break;
                case SDL_POWERSTATE_ON_BATTERY: state_str = "ON_BATTERY"; break;
                case SDL_POWERSTATE_NO_BATTERY: state_str = "NO_BATTERY"; break;
                case SDL_POWERSTATE_CHARGING:   state_str = "CHARGING";   break;
                case SDL_POWERSTATE_CHARGED:    state_str = "CHARGED";    break;
                default:                        state_str = "INVALID";    break;
            }

            if (dev.battery_state == SDL_POWERSTATE_UNKNOWN) {
                LOG_WARN(kTag, "Battery [%s]: State=%s (battery info not available)",
                        dev.name.c_str(), state_str);
                LOG_WARN(kTag, "Possible causes: hid_playstation not loaded, missing udev rules, or SDL can't read battery");
            } else if (dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
                LOG_DEBUG(kTag, "Battery [%s]: State=%s, Percent=%d%%",
                        dev.name.c_str(), state_str, percent);
            }
        }

        // ── Left Joy-Con battery (combined pair only) ─────────────────────
        // When two Joy-Cons are merged into one virtual gamepad, SDL exposes
        // SDL_SENSOR_GYRO_L on the combined handle.  In that case we find the
        // left Joy-Con's physical joystick by scanning all connected joystick
        // IDs for the sibling that SDL merged into this gamepad.
        // SDL_GetGamepadID() returns the instance_id of the *right* Joy-Con
        // (the primary half); the left half is the other HIDAPI joystick whose
        // type is GAMEPAD and whose instance_id differs from dev.instance_id.
        if (SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_L)) {
            dev.battery_state_L   = SDL_POWERSTATE_UNKNOWN;
            dev.battery_percent_L = -1;

            int joystick_count = 0;
            SDL_JoystickID* joystick_ids = SDL_GetJoysticks(&joystick_count);
            if (joystick_ids) {
                for (int i = 0; i < joystick_count; ++i) {
                    SDL_JoystickID jid = joystick_ids[i];
                    if (jid == dev.instance_id) continue;
                    if (SDL_GetJoystickTypeForID(jid) != SDL_JOYSTICK_TYPE_GAMEPAD) continue;

                    // Open transiently just to read battery; SDL ref-counts opens.
                    SDL_Joystick* joy = SDL_OpenJoystick(jid);
                    if (!joy) continue;

                    int leftPercent = 0;
                    SDL_PowerState leftState = SDL_GetJoystickPowerInfo(joy, &leftPercent);

                    if (leftState != SDL_POWERSTATE_NO_BATTERY) {
                        dev.battery_state_L   = leftState;
                        dev.battery_percent_L = leftPercent;
                        LOG_DEBUG(kTag, "Battery L [%s]: Percent=%d%%", dev.name.c_str(), leftPercent);
                        SDL_CloseJoystick(joy);
                        break;
                    }

                    SDL_CloseJoystick(joy);
                }
                SDL_free(joystick_ids);
            }
        } else {
            // Not a combined pair - clear the left-side fields.
            dev.battery_state_L   = SDL_POWERSTATE_UNKNOWN;
            dev.battery_percent_L = -1;
        }
        // ── End Left Joy-Con battery ──────────────────────────────────────
    } else if (dev.joystick) {
        int percent = 0;
        dev.battery_state = SDL_GetJoystickPowerInfo(dev.joystick, &percent);
        dev.battery_percent = percent;

        bool state_changed   = (old_state != dev.battery_state);
        bool percent_changed = (dev.battery_percent >= 0 && old_percent >= 0 &&
                                abs(old_percent - dev.battery_percent) >= 5);

        if (!dev.battery_initialized || state_changed || percent_changed) {
            dev.battery_initialized = true;

            const char* state_str;
            switch (dev.battery_state) {
                case SDL_POWERSTATE_UNKNOWN:    state_str = "UNKNOWN";    break;
                case SDL_POWERSTATE_ON_BATTERY: state_str = "ON_BATTERY"; break;
                case SDL_POWERSTATE_NO_BATTERY: state_str = "NO_BATTERY"; break;
                case SDL_POWERSTATE_CHARGING:   state_str = "CHARGING";   break;
                case SDL_POWERSTATE_CHARGED:    state_str = "CHARGED";    break;
                default:                        state_str = "INVALID";    break;
            }

            if (dev.battery_state != SDL_POWERSTATE_NO_BATTERY && dev.battery_state != SDL_POWERSTATE_UNKNOWN) {
                LOG_DEBUG(kTag, "Battery (Joystick) [%s]: State=%s, Percent=%d%%",
                        dev.name.c_str(), state_str, percent);
            }
        }
    } else {
        dev.battery_state   = SDL_POWERSTATE_UNKNOWN;
        dev.battery_percent = -1;
    }
}
// ---------------------------------------------------------------------------
// Device hide support
// ---------------------------------------------------------------------------

bool DeviceManager::IsHideAvailable() const {
#ifdef ENABLE_EXCLUSIVE_INPUT
    return m_HideManager.IsAvailable();
#else
    return false;
#endif
}

bool DeviceManager::SetDeviceHidden(DeviceState& dev, bool hidden) {
#ifdef ENABLE_EXCLUSIVE_INPUT
    SDL_Joystick* joy = dev.joystick
                         ? dev.joystick
                         : (dev.gamepad ? SDL_GetGamepadJoystick(dev.gamepad) : nullptr);
    if (!joy) {
        LOG_WARN(kTag, "SetDeviceHidden: no joystick handle for '%s'.",
                dev.name.c_str());
        return false;
    }

    bool ok = m_HideManager.SetHidden(joy, hidden);
    if (ok) dev.hide_from_other_apps = hidden;
    return ok;
#else
    (void)dev; (void)hidden;
    LOG_WARN(kTag, "SetDeviceHidden: exclusive input support not compiled in.");
    return false;
#endif
}

void DeviceManager::SetSteamInputCompatible(bool enabled) {
#ifdef ENABLE_EXCLUSIVE_INPUT
    m_HideManager.SetSteamInputCompatible(enabled);
#else
    (void)enabled;
#endif
}
