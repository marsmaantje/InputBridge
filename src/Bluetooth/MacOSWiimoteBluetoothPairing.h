#pragma once

#ifdef __APPLE__

#include "WiimoteBluetoothPairingImpl.h"

#include <memory>

namespace InputBridge::Bluetooth {

// macOS backend: Bluetooth Classic discovery + pairing via the IOBluetooth
// framework (IOBluetoothDeviceInquiry for discovery,
// IOBluetoothDevice openConnection: for pairing/connecting).
//
// IOBluetooth's discovery/pairing API is Objective-C only, so the actual
// implementation lives in MacOSWiimoteBluetoothPairing.mm (Objective-C++);
// this header and the pimpl below keep every Objective-C type out of the
// rest of the (plain C++) codebase, matching how MacOSExclusiveMode.h/.cpp
// stay plain C++ against IOKit's C API - the difference here is IOBluetooth
// genuinely has no C API for this, hence the .mm file and the pimpl.
//
// Calling -openConnection: on a discovered device triggers macOS's own
// Bluetooth daemon to perform standard baseband pairing (Just Works, same
// as the other two backends - see WiimoteBluetoothPairing.h) and, because
// a Wiimote advertises the HID service class, macOS's regular Bluetooth
// HID stack takes over from there automatically - the same path a
// physical Bluetooth mouse or keyboard pairs through. Once that finishes,
// the Wiimote enumerates as a normal HID device and WiimoteManager's
// existing SDL_hid-based scan (see WiimoteManager.h) finds it exactly like
// any other Wiimote.
//
// Needs `NSBluetoothAlwaysUsageDescription` present in the app's
// Info.plist (macOS shows a one-time permission prompt driven by that key
// before IOBluetooth will report anything) - InputBridge's CMakeLists.txt
// doesn't currently generate a custom Info.plist, so this must be added
// wherever the app's bundle Info.plist ends up being produced/signed.
// Written against the long-standing IOBluetoothDeviceInquiry/
// IOBluetoothDevice APIs (used the same way by e.g. the historical
// DarwiinRemote/WiinRemote Wiimote tools) but not build- or
// hardware-verified as part of this change.
class MacOSWiimoteBluetoothPairing : public WiimotePairingImpl {
public:
    MacOSWiimoteBluetoothPairing();
    ~MacOSWiimoteBluetoothPairing() override;

    bool IsAvailable() const override;

    void StartDiscovery(WiimotePairing::DeviceFoundCallback on_device,
                         WiimotePairing::DiscoveryDoneCallback on_done) override;
    void StopDiscovery() override;
    bool IsDiscovering() const override;

    void PairDevice(const std::string &address, WiimotePairing::PairCallback on_done) override;

    void Pump() override;

    // Public only so the Objective-C delegate classes defined in the .mm
    // file (which can't be C++ class members) can call back into this
    // instance's Impl without needing to be declared here too.
    struct Impl;

private:
    std::unique_ptr<Impl> m_Impl;
};

} // namespace InputBridge::Bluetooth

#endif // __APPLE__
