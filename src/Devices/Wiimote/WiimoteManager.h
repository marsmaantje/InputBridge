// src/Devices/Wiimote/WiimoteManager.h
//
// Owns raw-HID Wiimote devices independently of the SDL_Joystick-based
// DeviceManager/DeviceState list, since IR, Balance Board weight, and
// Guitar Hero frets have no representation in SDL's gamepad abstraction.
//
// Integration note: SDL_HINT_JOYSTICK_HIDAPI_WII should stay disabled/unset
// so SDL's own HIDAPI Wii driver doesn't also claim the device - see
// README.md for the coexistence strategy.
#pragma once
#include "WiimoteDevice.h"
#include <vector>
#include <memory>
#include <string>

namespace InputBridge::Wiimote {

class WiimoteManager {
public:
    // Enumerates connected Wii Remote / Wii Remote Plus / Balance Board HID
    // devices not already in `already_open_paths`, and opens+initializes
    // only the new ones.
    //
    // Always pass every path already held open: a device left out gets a
    // second concurrent SDL_hid handle running the full Init() sequence
    // against a device already mid-flight on the first handle. Concurrent
    // opens of one Bluetooth HID node are a known Linux (BlueZ/hidraw)
    // trouble spot - at best this races the two handles' writes, at worst
    // it's been observed crashing the process.
    //
    // Call on startup and whenever DeviceManager sees an unrecognized
    // SDL_EVENT_JOYSTICK_ADDED (or periodically, to catch devices paired
    // mid-session).
    //
    // `try_linux_l2cap`: Linux only. When true, makes one passive attempt
    // per new device to connect a direct L2CAP transport (see
    // Linux/WiimoteL2CAPTransport.h) before falling back to hidraw.
    // Defaults to false and should stay opt-in: even a refused connect()
    // sends real L2CAP signaling over the device's existing Bluetooth
    // link, and some third-party Wiimote chips have been observed
    // dropping IR data purely from receiving that request - not risk-free.
    static std::vector<std::unique_ptr<WiimoteDevice>> Scan(
        const std::vector<std::string> &already_open_paths = {},
        bool try_linux_l2cap = false);

    // True if `product` looks like a Wiimote product string SDL might
    // otherwise also try to claim as a generic gamepad.
    static bool IsWiimoteProductString(const char *product);

#if defined(__linux__)
    // True if the most recent Scan() failed to open at least one
    // Wiimote/Balance Board-shaped hidraw node specifically due to EACCES
    // (as opposed to it being transiently held by another process, or not
    // existing yet). Reset to false at the start of every Scan(), so this
    // reflects only the latest pass - once the udev rule is installed and
    // the device replugs successfully, this goes back to false on its own.
    //
    // Intended for the UI to offer "install udev rules to fix this" - see
    // DevicePanel.cpp - rather than that guidance only ever living in the
    // log.
    static bool HadRecentLinuxPermissionError();
#endif
};

} // namespace InputBridge::Wiimote
