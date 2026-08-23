// src/Devices/Wiimote/WiimoteManager.h
//
// Owns raw-HID Wiimote devices independently of the SDL_Joystick-based
// DeviceManager/DeviceState list, since neither IR data nor Balance Board
// weight data nor Guitar Hero frets have any representation in SDL's
// joystick/gamepad abstraction.
//
// Integration note: for this to see any Wiimotes, SDL_HINT_JOYSTICK_HIDAPI_WII
// should be left at its default ("0"/unset) or explicitly disabled, so SDL's
// own HIDAPI Wii driver does not also claim the device - see the design doc
// (README.md in this directory) for the recommended coexistence strategy.
#pragma once
#include "WiimoteDevice.h"
#include <vector>
#include <memory>
#include <string>

namespace InputBridge::Wiimote {

class WiimoteManager {
public:
    // Enumerates all currently-connected Wii Remote / Wii Remote Plus /
    // Wii Balance Board HID devices NOT already present in
    // `already_open_paths`, and opens+initializes only the new ones.
    //
    // Skipping already-tracked paths is not just an efficiency nicety: a
    // device left out of `already_open_paths` gets a *second* concurrent
    // SDL_hid handle opened to the same physical HID node, and that second
    // handle immediately runs the full Init() sequence (data-report-mode,
    // IR camera, LED writes) against a device that's already mid-flight on
    // the first handle. Concurrent opens of one Bluetooth HID node are a
    // known trouble spot on Linux (BlueZ/hidraw) - at best this causes the
    // two handles' writes to race each other and flap the reporting mode;
    // at worst it's been observed crashing the process outright. Always
    // pass every path you already hold open.
    //
    // Call on startup and whenever DeviceManager sees an
    // SDL_EVENT_JOYSTICK_ADDED for a device SDL itself doesn't recognize as
    // something else (or periodically, e.g. every few seconds, to catch
    // devices paired while the app is running).
    static std::vector<std::unique_ptr<WiimoteDevice>> Scan(
        const std::vector<std::string> &already_open_paths = {});

    // Convenience: true if `name`/`product_string` looks like a Wiimote
    // product SDL might otherwise also try to claim as a generic gamepad.
    static bool IsWiimoteProductString(const char *product);
};

} // namespace InputBridge::Wiimote
