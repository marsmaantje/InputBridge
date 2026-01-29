#include "DeviceManager.h"
#include <algorithm>

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
        }
    }
}

void DeviceManager::HandleDeviceRemoved(SDL_JoystickID instance_id) {
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
}
