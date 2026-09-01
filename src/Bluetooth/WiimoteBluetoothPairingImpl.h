// src/Bluetooth/WiimoteBluetoothPairingImpl.h
//
// Abstract platform backend for WiimotePairing (mirrors the
// InputExclusiveModeImpl pattern in src/ExclusiveMode/) - one
// implementation per OS Bluetooth stack:
//   Linux   - BlueZ over D-Bus (LinuxWiimoteBluetoothPairing)
//   Windows - WinRT Windows.Devices.Enumeration/Bluetooth (WindowsWiimoteBluetoothPairing)
//   macOS   - IOBluetooth (MacOSWiimoteBluetoothPairing)
#pragma once

#include "WiimoteBluetoothPairing.h"

namespace InputBridge::Bluetooth {

class WiimotePairingImpl {
public:
    virtual ~WiimotePairingImpl() = default;

    virtual bool IsAvailable() const = 0;

    virtual void StartDiscovery(WiimotePairing::DeviceFoundCallback on_device,
                                 WiimotePairing::DiscoveryDoneCallback on_done) = 0;
    virtual void StopDiscovery() = 0;
    virtual bool IsDiscovering() const = 0;

    virtual void PairDevice(const std::string &address, WiimotePairing::PairCallback on_done) = 0;

    // Default no-op is fine for backends that deliver callbacks
    // synchronously from within their own OS-driven callback as long as
    // that callback already happens to land on the caller's thread; every
    // backend here still queues instead, to keep the "callbacks only ever
    // come from Pump()" guarantee backend-independent rather than each
    // caller having to know which platform is safe to skip Pump() on.
    virtual void Pump() {}
};

} // namespace InputBridge::Bluetooth
