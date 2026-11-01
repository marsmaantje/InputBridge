#pragma once
#include "DeviceState.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <memory>
#include <map>

#include "Haptics/HapticDevice.h"
#include "Devices/Wiimote/WiimoteManager.h"
#include "Devices/SpaceMouse/SpaceMouseManager.h"

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

    // -- Wiimote / Balance Board / Nunchuk / Classic Controller / Guitar Hero --
    // Driven directly over raw HID (see Devices/Wiimote/README.md) - these
    // are NOT SDL_Joystick-backed DeviceState entries in m_Devices, since
    // IR/Balance-Board/Guitar data has no representation in SDL's gamepad
    // abstraction. ScanWiimotes() is idempotent: already-tracked devices
    // (matched by HID path) are left alone, new ones are appended.
    void ScanWiimotes();
    const std::vector<std::unique_ptr<InputBridge::Wiimote::WiimoteDevice>>& GetWiimotes() const;

    // Resolves a WiimoteVirtualBridge bridge joystick's SDL_JoystickID (as
    // seen on a normal DeviceState/HapticTarget) back to the real
    // WiimoteDevice that backs it, so callers that only deal in
    // SDL_JoystickID (e.g. OutputMapper routing a rumble command) can reach
    // WiimoteDevice::SetRumble() - the bridge joystick itself is virtual
    // and has no SDL_Haptic backing. Returns nullptr if instance_id isn't a
    // currently-tracked Wiimote bridge joystick, or if it belongs to a
    // Balance Board (which has no rumble motor).
    InputBridge::Wiimote::WiimoteDevice* GetWiimoteForBridgeJoystick(SDL_JoystickID instance_id) const;

    // -- 3Dconnexion 6-DOF devices (SpaceNavigator, SpaceMouse, ...) ---------
    // Read passively over shared HID (see Devices/SpaceMouse/README.md) and
    // bridged into virtual SDL joysticks. Never opens a device exclusively,
    // never writes to it, so 3DxWare / spacenavd keep working alongside.
    // ScanSpaceMice() is idempotent and also prunes devices that went away.
    void ScanSpaceMice();
    const std::vector<std::unique_ptr<InputBridge::SpaceMouse::SpaceMouseDevice>>& GetSpaceMice() const;
    // Master switch (Settings). When off, no 3Dconnexion HID interface is
    // opened and SDL's own generic handling of these devices is left alone.
    void SetSpaceMouseEnabled(bool enabled);
    bool IsSpaceMouseEnabled() const { return m_SpaceMouseEnabled; }

    // -- Device hide (HidHide / evdev grab / IOKit seize) ---------------------
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

    std::vector<std::unique_ptr<InputBridge::Wiimote::WiimoteDevice>> m_Wiimotes;
    Uint64 m_LastWiimoteScanMs = 0;
    static constexpr Uint64 kWiimoteScanIntervalMs = 3000; // BT pairing happens outside SDL's joystick events

    std::vector<std::unique_ptr<InputBridge::SpaceMouse::SpaceMouseDevice>> m_SpaceMice;
    Uint64 m_LastSpaceMouseScanMs = 0;
    bool   m_SpaceMouseEnabled = true;
    static constexpr Uint64 kSpaceMouseScanIntervalMs = 3000; // hotplug without SDL events

#ifdef ENABLE_EXCLUSIVE_INPUT
    InputExclusiveMode m_HideManager;
#endif
};
