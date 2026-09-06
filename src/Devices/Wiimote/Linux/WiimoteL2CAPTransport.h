// src/Devices/Wiimote/Linux/WiimoteL2CAPTransport.h
//
// Linux-only transport that talks to a Wiimote directly over raw
// Bluetooth L2CAP sockets (control PSM 0x11, interrupt PSM 0x13),
// bypassing the OS's generic HID subsystem (hidraw) entirely - and
// therefore anything else sharing that hidraw node, most notably Steam
// Input silently resetting our data-reporting mode (README.md's "IR
// camera doesn't work while Steam is running"). Same architecture
// Dolphin's real-Wiimote support has always used.
//
// Implemented against the stable AF_BLUETOOTH/L2CAP kernel socket ABI
// directly (own bdaddr_t/sockaddr_l2 definitions below) rather than
// linking libbluetooth, avoiding a new build dependency for what's a
// handful of struct definitions and two socket() calls.
//
// Per WiiBrew's Bluetooth HID framing, output reports get an 0xA2 prefix
// and input reports arrive with 0xA1 - added/stripped here so the report
// bytes WiimoteDevice sees match what WiimoteHidTransport already hands
// it (hidraw strips this same framing invisibly on that path).
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
    // Connects both L2CAP channels to `bdaddr` (human-reading order, i.e.
    // ParseBluetoothAddress()'s output - not the wire's LSB-first order;
    // converted internally). Returns nullptr if either connection fails
    // (most commonly: the OS's own Bluetooth HID service already holds
    // those PSMs for this device - see
    // WiimoteBluetoothUtil::DisconnectExistingHidConnection(), which
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
