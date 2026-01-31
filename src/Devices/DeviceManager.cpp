#include "DeviceManager.h"
#include <algorithm>

DeviceManager& DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

DeviceManager::DeviceManager() {}
DeviceManager::~DeviceManager() { CloseAllDevices(); }

const std::vector<DeviceState> &DeviceManager::GetDevices() const {
    return m_Devices;
}

std::string DeviceManager::GetDeviceGUIDString(const DeviceState &dev) {
    SDL_GUID guid = SDL_GetJoystickGUID(dev.joystick);
    char guidStr[33];
    SDL_GUIDToString(guid, guidStr, sizeof(guidStr));
    return std::string(guidStr);
}

void DeviceManager::HandleDeviceAdded(SDL_JoystickID instance_id) {
    if (SDL_IsGamepad(instance_id)) {
        SDL_Gamepad *gamepad = SDL_OpenGamepad(instance_id);
        if (gamepad) {
            DeviceState dev;
            dev.instance_id = instance_id;
            dev.name = SDL_GetGamepadName(gamepad);
            dev.is_gamepad = true;
            dev.gamepad = gamepad;
            dev.joystick = SDL_GetGamepadJoystick(gamepad);
            dev.num_axes = SDL_GetNumJoystickAxes(dev.joystick);
            dev.num_buttons = SDL_GetNumJoystickButtons(dev.joystick);
            dev.num_hats = SDL_GetNumJoystickHats(dev.joystick);
            m_Devices.push_back(dev);

            bool is_haptic = SDL_IsJoystickHaptic(dev.joystick);
            if (is_haptic || dev.is_gamepad) {
                 if (dev.name.find("wheel") != std::string::npos) {
                    if (is_haptic) {
                        m_HapticDevices[instance_id] = std::make_unique<SteeringWheelHaptics>(dev.joystick);
                    }
                } else {
                    m_HapticDevices[instance_id] = std::make_unique<GamepadHaptics>(dev.joystick);
                }
                if (m_HapticDevices[instance_id]) {
                    m_HapticDevices[instance_id]->Init();
                }
            }
        }
    } else {
        SDL_Joystick *joystick = SDL_OpenJoystick(instance_id);
        if (joystick) {
            DeviceState dev;
            dev.instance_id = instance_id;
            dev.name = SDL_GetJoystickName(joystick);
            dev.is_gamepad = false;
            dev.gamepad = nullptr;
            dev.joystick = joystick;
            dev.num_axes = SDL_GetNumJoystickAxes(joystick);
            dev.num_buttons = SDL_GetNumJoystickButtons(joystick);
            dev.num_hats = SDL_GetNumJoystickHats(joystick);
            m_Devices.push_back(dev);

            if (SDL_IsJoystickHaptic(joystick)) {
                if (dev.name.find("wheel") != std::string::npos) {
                    m_HapticDevices[instance_id] = std::make_unique<SteeringWheelHaptics>(joystick);
                } else {
                    m_HapticDevices[instance_id] = std::make_unique<GamepadHaptics>(joystick);
                }
                if (m_HapticDevices[instance_id]) {
                    m_HapticDevices[instance_id]->Init();
                }
            }
        }
    }
}

void DeviceManager::HandleDeviceRemoved(SDL_JoystickID instance_id) {
    m_HapticDevices.erase(instance_id);
    auto it = std::remove_if(m_Devices.begin(), m_Devices.end(),
                             [instance_id](const DeviceState &dev) {
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

HapticDevice* DeviceManager::GetHapticDevice(SDL_JoystickID instance_id) {
    auto it = m_HapticDevices.find(instance_id);
    if (it != m_HapticDevices.end()) {
        return it->second.get();
    }
    return nullptr;
}
