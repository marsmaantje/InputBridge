#pragma once

#ifdef _WIN32

#include "WiimoteBluetoothPairingImpl.h"

#include <memory>

namespace InputBridge::Bluetooth {

// Windows backend: Bluetooth Classic discovery + pairing via WinRT
// (Windows.Devices.Enumeration / Windows.Devices.Bluetooth), the same API
// Windows Settings' own "Add a Bluetooth device" flow is built on.
//
// The WinRT/C++-WinRT types themselves (winrt::Windows::..., coroutines,
// apartment initialization) are kept entirely inside the .cpp behind a
// pimpl - this header stays plain C++ so nothing elsewhere in InputBridge
// that includes it needs the cppwinrt headers or /await-style build setup.
//
// Requires a Windows SDK new enough to ship the prebuilt `<winrt/...>`
// headers for the base namespaces (10.0.17134+ in practice) and linking
// windowsapp.lib - see CMakeLists.txt. This has been written against the
// documented WinRT device-pairing pattern (Microsoft's own
// "DeviceEnumerationAndPairing" sample uses the same AEP/custom-pairing
// approach) but has not been build- or hardware-verified as part of this
// change - please validate the AQS filter string and pairing-kind handling
// in WindowsWiimoteBluetoothPairing.cpp against a real Windows box with a
// Wiimote before shipping.
class WindowsWiimoteBluetoothPairing : public WiimotePairingImpl {
public:
    WindowsWiimoteBluetoothPairing();
    ~WindowsWiimoteBluetoothPairing() override;

    bool IsAvailable() const override;

    void StartDiscovery(WiimotePairing::DeviceFoundCallback on_device,
                         WiimotePairing::DiscoveryDoneCallback on_done) override;
    void StopDiscovery() override;
    bool IsDiscovering() const override;

    void PairDevice(const std::string &address, WiimotePairing::PairCallback on_done) override;
    void ConnectDevice(const std::string &address, WiimotePairing::PairCallback on_done) override;

    void Pump() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace InputBridge::Bluetooth

#endif // _WIN32
