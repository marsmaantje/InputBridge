// src/Devices/Wiimote/WiimoteTransport.h
//
// Abstracts how WiimoteDevice moves bytes to/from the physical Wiimote.
// Two implementations:
//
//   WiimoteHidTransport  - SDL_hid_* (hidraw/HidD_*/IOHIDManager). Shares
//                          the OS HID device node with other processes,
//                          including Steam Input (see README.md's "IR
//                          camera doesn't work while Steam is running").
//
//   WiimoteL2CAPTransport - Linux only. Talks directly over raw Bluetooth
//                          L2CAP sockets, bypassing the OS HID subsystem
//                          (and Steam Input) entirely - the same approach
//                          Dolphin's real-Wiimote support uses.
//
// Method semantics mirror the SDL_hid_* calls WiimoteDevice used to make
// directly (report ID as first byte, non-blocking Read returns 0 when
// idle), so WiimoteDevice.cpp doesn't need to know which transport it's
// using. Wire data is passed as std::span<std::byte> - the transports are
// the only place that converts to/from the unsigned char* the OS/SDL APIs
// want.
#pragma once
#include <cstddef>
#include <span>

namespace InputBridge::Wiimote {

class IWiimoteTransport {
public:
    virtual ~IWiimoteTransport() = default;

    // True if connected and usable for I/O; once false, treat as gone.
    virtual bool IsOpen() const = 0;

    // Writes one full output report; data[0] is the report ID (matches
    // SDL_hid_write). WiimoteL2CAPTransport adds the WiiBrew Bluetooth HID
    // "0xA2 DATA|Output" framing underneath, not the caller. Returns bytes
    // accepted, or -1 on error.
    virtual int Write(std::span<const std::byte> data) = 0;

    // Non-blocking read of the next pending input report (buf[0] = report
    // ID on success). Returns bytes read, 0 if nothing pending, or -1 on
    // error/disconnect.
    virtual int Read(std::span<std::byte> buf) = 0;

    // Closes the connection. Safe to call more than once or from a
    // destructor.
    virtual void Close() = 0;
};

} // namespace InputBridge::Wiimote
