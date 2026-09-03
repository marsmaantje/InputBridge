// src/Devices/Wiimote/Linux/WiimoteL2CAPTransport.h
//
// Linux-only transport that talks to a Wiimote directly over raw
// Bluetooth L2CAP sockets (control PSM 0x11, interrupt PSM 0x13),
// bypassing the OS's generic HID subsystem (hidraw / hid-generic)
// entirely - and therefore anything else sharing that hidraw node with
// us, most notably Steam Input silently resetting our data-reporting
// mode (see Devices/Wiimote/README.md's "IR camera doesn't work while
// Steam is running" section). This mirrors the architecture Dolphin's
// real-Wiimote support has always used, which is why Dolphin was never
// affected by that conflict in the first place.
//
// Deliberately implemented against the stable AF_BLUETOOTH/L2CAP kernel
// socket ABI directly (own bdaddr_t/sockaddr_l2 definitions below) rather
// than linking libbluetooth (BlueZ's userspace convenience library) -
// that ABI has been stable since the Bluetooth socket family was added to
// the kernel and this avoids adding a new build dependency for what's
// otherwise a handful of struct definitions and two socket() calls.
//
// Per the WiiBrew-documented Bluetooth HID framing, output reports get an
// 0xA2 ("DATA | Output") prefix byte and input reports arrive with an
// 0xA1 ("DATA | Input") prefix - both added/stripped here so the report
// bytes WiimoteDevice sees are identical to what WiimoteHidTransport
// already hands it (hidapi/the kernel's hid-generic driver strips this
// same framing for the hidraw path, invisibly to this codebase).
#pragma once
#ifdef __linux__

#include "Devices/Wiimote/WiimoteTransport.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace InputBridge::Wiimote {

class WiimoteL2CAPTransport : public IWiimoteTransport {
public:
    // Connects both the control and interrupt L2CAP channels to
    // `bdaddr` (6 raw address bytes, human-reading order - i.e.
    // ParseBluetoothAddress()'s output, NOT the on-the-wire
    // least-significant-byte-first order the kernel struct wants; the
    // conversion happens inside this call). Returns nullptr if either
    // connection fails (e.g. the OS's own Bluetooth HID service is
    // already holding those PSMs open for this device - see
    // WiimoteBluetoothUtil.h's DisconnectExistingHidConnection(), which
    // callers should try first).
    static std::unique_ptr<WiimoteL2CAPTransport> Connect(const std::array<uint8_t, 6> &bdaddr);

    ~WiimoteL2CAPTransport() override;

    WiimoteL2CAPTransport(const WiimoteL2CAPTransport &) = delete;
    WiimoteL2CAPTransport &operator=(const WiimoteL2CAPTransport &) = delete;

    bool IsOpen() const override { return m_InterruptFd >= 0; }
    int Write(const uint8_t *data, size_t len) override;
    int Read(uint8_t *buf, size_t bufsize) override;
    void Close() override;

private:
    WiimoteL2CAPTransport(int control_fd, int interrupt_fd);

    int m_ControlFd = -1;
    int m_InterruptFd = -1;
};

} // namespace InputBridge::Wiimote

#endif // __linux__
