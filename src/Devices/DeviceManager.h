#pragma once
#include "DeviceState.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <memory>
#include <map>

#include "Haptics/HapticDevice.h"
#include "wheel/wheel_manager.hpp"

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

    // --- wheel-rpm-lib integration ---
    // Re-scans HID devices and refreshes the list of RPM-capable wheels.
    // Safe to call at any time; replaces the previous scan result.
    void ScanWheelRPMDevices();

    // Returns the currently-known RPM-capable wheel devices.
    const std::vector<std::unique_ptr<wheel::Wheel>>& GetWheelRPMDevices() const;

  private:
    DeviceManager();
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    std::vector<DeviceState> m_Devices;
    std::map<SDL_JoystickID, std::unique_ptr<HapticDevice>> m_HapticDevices;

    // Wheel RPM devices discovered via wheel-rpm-lib
    std::vector<std::unique_ptr<wheel::Wheel>> m_WheelRPMDevices;
};
