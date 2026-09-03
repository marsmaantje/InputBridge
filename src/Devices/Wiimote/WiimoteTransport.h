// src/Devices/Wiimote/WiimoteTransport.h
//
// Abstracts how WiimoteDevice actually moves bytes to/from the physical
// Wiimote. Two implementations exist:
//
//   WiimoteHidTransport  - the original path: SDL_hid_* (hidraw on Linux,
//                           HidD_* on Windows, IOHIDManager on macOS). This
//                           shares the OS's generic HID device node with
//                           every other process that also opens it -
//                           including Steam Input, which is the documented
//                           cause of "IR camera doesn't work while Steam is
//                           running" (see README.md in this directory).
//
//   WiimoteL2CAPTransport - Linux only. Talks directly to the Wiimote over
//                           its own raw Bluetooth L2CAP control/interrupt
//                           sockets, bypassing the OS HID subsystem (and
//                           therefore anything else sharing it) entirely.
//                           This is the same architecture Dolphin's real-
//                           Wiimote support uses, and why Dolphin was never
//                           affected by the Steam Input conflict in the
//                           first place - it was never sharing the hidraw
//                           node to begin with.
//
// Every method's semantics intentionally mirror the SDL_hid_* calls
// WiimoteDevice used to make directly (report ID as the first byte of both
// Write's input and Read's output, non-blocking Read returning 0 when
// nothing is pending), so swapping the transport underneath it doesn't
// change any of the protocol-level code in WiimoteDevice.cpp - that code
// has no idea which transport it's talking to.
#pragma once
#include <cstddef>
#include <cstdint>

namespace InputBridge::Wiimote {

class IWiimoteTransport {
public:
    virtual ~IWiimoteTransport() = default;

    // True if this transport is currently connected and usable for I/O.
    // WiimoteDevice checks this the same way it used to check `m_Dev` for
    // null - once false, treat the device as gone.
    virtual bool IsOpen() const = 0;

    // Writes one full output report; `data[0]` is the report ID, matching
    // SDL_hid_write's convention (and, for WiimoteL2CAPTransport, the
    // WiiBrew-documented Bluetooth HID "0xA2 DATA|Output" framing is added
    // underneath this call, not by the caller). Returns the number of
    // bytes accepted, or -1 on error.
    virtual int Write(const uint8_t *data, size_t len) = 0;

    // Non-blocking read of the next pending input report into `buf` (up to
    // `bufsize` bytes; `buf[0]` is the report ID on success). Returns the
    // number of bytes read, 0 if nothing is pending right now, or -1 on
    // error/disconnect.
    virtual int Read(uint8_t *buf, size_t bufsize) = 0;

    // Closes the underlying connection/socket(s). Safe to call more than
    // once and safe to call from a destructor.
    virtual void Close() = 0;
};

} // namespace InputBridge::Wiimote
