#include "DeviceManager.h"
#include "Preferences.h"

std::vector<DeviceState> g_Devices;

std::string GetDeviceGUIDString(const DeviceState& dev) {
    SDL_GUID guid = SDL_GetJoystickGUID(dev.joystick);
    char guidStr[33];
    SDL_GUIDToString(guid, guidStr, sizeof(guidStr));
    return std::string(guidStr);
}

void HandleDeviceAdded(SDL_JoystickID instance_id) {
    if (SDL_IsGamepad(instance_id)) {
        SDL_Gamepad* gamepad = SDL_OpenGamepad(instance_id);
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
            g_Devices.push_back(dev);
        }
    } else {
        SDL_Joystick* joystick = SDL_OpenJoystick(instance_id);
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
            g_Devices.push_back(dev);
        }
    }
}

void HandleDeviceRemoved(SDL_JoystickID instance_id) {
    for (auto it = g_Devices.begin(); it != g_Devices.end(); ++it) {
        if (it->instance_id == instance_id) {
            if (it->gamepad) SDL_CloseGamepad(it->gamepad);
            else if (it->joystick) SDL_CloseJoystick(it->joystick);
            g_AppliedPreferences.erase(instance_id);
            g_Devices.erase(it);
            break;
        }
    }
}

void CloseAllDevices() {
    for (auto& dev : g_Devices) {
        if (dev.gamepad) SDL_CloseGamepad(dev.gamepad);
        else if (dev.joystick) SDL_CloseJoystick(dev.joystick);
    }
    g_Devices.clear();
}
