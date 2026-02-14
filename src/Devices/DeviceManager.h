#pragma once
#include "DeviceState.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <memory>
#include <map>

#include "Haptics/HapticDevice.h"

class DeviceManager {
  public:
    static DeviceManager& GetInstance();

    ~DeviceManager();

    void HandleDeviceAdded(SDL_JoystickID instance_id);
    void HandleDeviceRemoved(SDL_JoystickID instance_id);
    void CloseAllDevices();

    const std::vector<DeviceState> &GetDevices() const;
    static std::string GetDeviceGUIDString(const DeviceState &dev);

    HapticDevice* GetHapticDevice(SDL_JoystickID instance_id) const;
    
    void UpdateBatteryInfo(DeviceState &dev);

  private:
    DeviceManager();
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    std::vector<DeviceState> m_Devices;
    std::map<SDL_JoystickID, std::unique_ptr<HapticDevice>> m_HapticDevices;
};
