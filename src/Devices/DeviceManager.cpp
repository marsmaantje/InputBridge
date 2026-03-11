#include "DeviceManager.h"
#include "DeviceFactory.h"
#include "SDL3/SDL_joystick.h"
#include "SDL3/SDL_log.h"
#include <algorithm>
#include <cstdlib>

DeviceManager &DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

DeviceManager::DeviceManager() {}
DeviceManager::~DeviceManager() { CloseAllDevices(); }

const std::vector<DeviceState> &DeviceManager::GetDevices() const { return m_Devices; }

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
        SDL_Log("Failed to create device %d", instance_id);
        return;
    }
    
    m_Devices.push_back(std::move(result->state));
    
    // Initialize battery info for the new device
    UpdateBatteryInfo(m_Devices.back());
    
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

// ---------------------------------------------------------------------------
// wheel-rpm-lib integration
// ---------------------------------------------------------------------------

void DeviceManager::ScanWheelRPMDevices() {
    m_WheelRPMDevices = wheel::WheelManager::scan();
    SDL_Log("WheelRPM scan complete: %zu device(s) found",
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
                SDL_Log("Battery [%s]: State=%s (battery info not available)",
                        dev.name.c_str(), state_str);
                SDL_Log("  Possible causes: hid_playstation not loaded, missing udev rules, or SDL can't read battery");
            } else if (dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
                SDL_Log("Battery [%s]: State=%s, Percent=%d%%",
                        dev.name.c_str(), state_str, percent);
            }
        }
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
                SDL_Log("Battery (Joystick) [%s]: State=%s, Percent=%d%%",
                        dev.name.c_str(), state_str, percent);
            }
        }
    } else {
        dev.battery_state   = SDL_POWERSTATE_UNKNOWN;
        dev.battery_percent = -1;
    }
}