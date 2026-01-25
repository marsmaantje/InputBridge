#pragma once
#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include "DeviceState.h"

class DeviceManager {
public:
    ~DeviceManager();

    void HandleDeviceAdded(SDL_JoystickID instance_id);
    void HandleDeviceRemoved(SDL_JoystickID instance_id);
    void CloseAllDevices();

    const std::vector<DeviceState>& GetDevices() const;
    static std::string GetDeviceGUIDString(const DeviceState& dev);

private:
    std::vector<DeviceState> m_Devices;
};
