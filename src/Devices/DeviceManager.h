#pragma once
#include "DeviceState.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <memory>
#include <map>

#include "Haptics/HapticDevice.h"

#ifdef ENABLE_EXCLUSIVE_INPUT
#include "ExclusiveMode/InputExclusiveMode.h"
#endif

class DeviceManager {
  public:
    static DeviceManager& GetInstance();

    ~DeviceManager();

    void HandleDeviceAdded(SDL_JoystickID instance_id);
    void HandleDeviceRemoved(SDL_JoystickID instance_id);
    void CloseAllDevices();
    void Update(bool isMinimized = false);
    void SetBatteryUpdateInterval(int ms) { m_BatteryUpdateIntervalMs = (Uint64)ms; }
    int  GetBatteryUpdateInterval() const { return (int)m_BatteryUpdateIntervalMs; }

    const std::vector<DeviceState>& GetDevices() const;
    std::vector<DeviceState>&       GetDevices();
    static std::string GetDeviceGUIDString(const DeviceState& dev);

    HapticDevice* GetHapticDevice(SDL_JoystickID instance_id) const;
    void SetDeviceKeepalive(SDL_JoystickID instance_id, bool enable);

    void UpdateBatteryInfo(DeviceState& dev);

    // ── Device hide (HidHide / evdev grab / IOKit seize) ─────────────────────
    // Toggle the hide state for a single device.  Updates dev.hide_from_other_apps
    // and calls through to the platform backend.
    // Returns true on success; false when the backend is unavailable or fails.
    bool SetDeviceHidden(DeviceState& dev, bool hidden);

    // True when the platform hide mechanism is available on this system.
    bool IsHideAvailable() const;

    // Allow/disallow Steam Input from seeing hidden devices (Windows only).
    void SetSteamInputCompatible(bool enabled);

  private:
    DeviceManager();
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    std::vector<DeviceState> m_Devices;
    std::map<SDL_JoystickID, std::unique_ptr<HapticDevice>> m_HapticDevices;
    Uint64 m_BatteryUpdateIntervalMs = 5000;

#ifdef ENABLE_EXCLUSIVE_INPUT
    InputExclusiveMode m_HideManager;
#endif
};