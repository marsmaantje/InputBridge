// src/Devices/Wiimote/WiimoteHidTransport.h
//
// The original transport: talks to the Wiimote through the OS's generic
// HID subsystem via hidapi (hidraw on Linux, HidD_* on Windows,
// IOHIDManager on macOS). See WiimoteTransport.h for why an alternative
// (WiimoteL2CAPTransport, Linux only) exists alongside this one.
#pragma once
#include "WiimoteTransport.h"
#include <SDL3/SDL_hidapi.h>

namespace InputBridge::Wiimote {

class WiimoteHidTransport : public IWiimoteTransport {
public:
    // Takes ownership of `dev` (already opened via SDL_hid_open_path).
    // Puts it into non-blocking mode immediately, same as the constructor
    // this logic used to live in directly on WiimoteDevice.
    explicit WiimoteHidTransport(SDL_hid_device *dev);
    ~WiimoteHidTransport() override;

    WiimoteHidTransport(const WiimoteHidTransport &) = delete;
    WiimoteHidTransport &operator=(const WiimoteHidTransport &) = delete;

    bool IsOpen() const override { return m_Dev != nullptr; }
    int Write(const uint8_t *data, size_t len) override;
    int Read(uint8_t *buf, size_t bufsize) override;
    void Close() override;

private:
    SDL_hid_device *m_Dev;
};

} // namespace InputBridge::Wiimote
