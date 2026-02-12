#include "DeviceManager.h"
#include "DeviceFactory.h"
#include "SDL3/SDL_joystick.h"
#include "SDL3/SDL_log.h"
#include <algorithm>

DeviceManager &DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

DeviceManager::DeviceManager() {}
DeviceManager::~DeviceManager() { CloseAllDevices(); }

const std::vector<DeviceState> &DeviceManager::GetDevices() const { return m_Devices; }

std::string DeviceManager::GetDeviceGUIDString(const DeviceState &dev) {
    SDL_GUID guid = SDL_GetJoystickGUID(dev.joystick);
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
}

void DeviceManager::HandleDeviceRemoved(SDL_JoystickID instance_id) {
    m_HapticDevices.erase(instance_id);
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
}

void DeviceManager::CloseAllDevices() {
    for (auto &dev : m_Devices) {
        if (dev.gamepad)
            SDL_CloseGamepad(dev.gamepad);
        else if (dev.joystick)
            SDL_CloseJoystick(dev.joystick);
    }
    m_Devices.clear();
    m_HapticDevices.clear();
}

HapticDevice *DeviceManager::GetHapticDevice(SDL_JoystickID instance_id) const {
    auto it = m_HapticDevices.find(instance_id);
    if (it != m_HapticDevices.end()) {
        return it->second.get();
    }
    return nullptr;
}

void DeviceManager::UpdateBatteryInfo(DeviceState &dev) {
    if (dev.gamepad) {
        // Get battery info from gamepad
        int percent = 0;
        dev.battery_state = SDL_GetGamepadPowerInfo(dev.gamepad, &percent);
        dev.battery_percent = percent;
    } else if (dev.joystick) {
        // Get battery info from joystick
        int percent = 0;
        dev.battery_state = SDL_GetJoystickPowerInfo(dev.joystick, &percent);
        dev.battery_percent = percent;
    } else {
        dev.battery_state = SDL_POWERSTATE_UNKNOWN;
        dev.battery_percent = -1;
    }
}
