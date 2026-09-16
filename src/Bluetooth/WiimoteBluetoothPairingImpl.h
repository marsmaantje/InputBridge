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

    // See WiimoteBluetoothPairing.h's ConnectDevice() comment. Pure
    // virtual, not defaulted - every backend must still deliver its result
    // via that backend's own queue/Pump() plumbing like every other
    // callback, even backends that don't have a real implementation and
    // just want to report PairResult::NotAvailable; a same-call-stack,
    // synchronous invocation of on_done() here would violate Pump()'s
    // "callbacks only ever arrive from Pump()" contract and risk
    // reentrancy bugs in whatever UI code called ConnectDevice() (e.g.
    // mutating a container the caller is still iterating over).
    virtual void ConnectDevice(const std::string &address, WiimotePairing::PairCallback on_done) = 0;

    // Default no-op is fine for backends that deliver callbacks
    // synchronously from within their own OS-driven callback as long as
    // that callback already happens to land on the caller's thread; every
    // backend here still queues instead, to keep the "callbacks only ever
    // come from Pump()" guarantee backend-independent rather than each
    // caller having to know which platform is safe to skip Pump() on.
    virtual void Pump() {}
};

} // namespace InputBridge::Bluetooth
