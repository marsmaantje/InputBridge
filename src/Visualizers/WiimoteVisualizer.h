// src/Visualizers/WiimoteVisualizer.h
#pragma once
#include "DeviceVisualizer.h"
#include "Devices/Wiimote/WiimoteDevice.h"

class WiimoteVisualizer : public DeviceVisualizer {
  public:
    void Draw(const DeviceState &dev) override;

    // Real Wiimote/Balance Board/Nunchuk/Classic Controller/Guitar Hero
    // rendering, driven by WiimoteManager's raw-HID snapshot rather than an
    // SDL_Joystick-backed DeviceState (see Devices/Wiimote/README.md - SDL's
    // Wii driver is disabled, so DeviceState-backed Wiimotes no longer occur
    // in practice; this is the path actually used going forward). `index`
    // distinguishes multiple connected Wiimotes/Balance Boards for widgets
    // that need per-device persistent UI state (e.g. Balance Board's dot-
    // size mode toggle) despite this visualizer being a single shared
    // instance across all of them - same convention DevicePanel already
    // uses for its player-LED/rumble controls.
    void Draw(const InputBridge::Wiimote::WiimoteSnapshot &snap, int index = 0);
};
