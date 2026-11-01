// src/Devices/SpaceMouse/SpaceMouseDevice.h
//
// One physical 3Dconnexion device, read passively over HID.
//
// Coexistence contract (see README.md in this folder): the device is opened
// through the OS's shared HID path (SDL_hid_*), only ever READ, and no
// output/feature report is ever sent. Every other reader of the same device
// (3DxWare, spacenavd, a CAD application) keeps receiving its own copy of
// every report.
#pragma once
#include "SpaceMouseProtocol.h"
#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_stdinc.h>
#include <string>

namespace InputBridge::SpaceMouse {

struct SpaceMouseSnapshot {
    std::string hid_path;
    std::string serial;       // may be empty
    ModelInfo   model{};
    State       state;
    Uint64      last_report_ms = 0; // informational only: an idle device sends nothing
    bool        connected      = true;
};

class SpaceMouseDevice {
public:
    // Takes ownership of `dev` (already opened with SDL_hid_open_path).
    SpaceMouseDevice(SDL_hid_device *dev, std::string path, std::string serial, const ModelInfo &model);
    ~SpaceMouseDevice();

    SpaceMouseDevice(const SpaceMouseDevice &) = delete;
    SpaceMouseDevice &operator=(const SpaceMouseDevice &) = delete;

    // Drains pending reports (bounded per call). Non-blocking. A read error
    // (device unplugged) flips Snapshot().connected to false.
    void Poll();

    const SpaceMouseSnapshot &Snapshot() const { return m_Snap; }

private:
    void Close();
    void ReleaseInputs();

    SDL_hid_device    *m_Dev;
    SpaceMouseSnapshot m_Snap;
};

} // namespace InputBridge::SpaceMouse
