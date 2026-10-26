// src/Devices/SpaceMouse/SpaceMouseManager.h
//
// Enumerates 3Dconnexion 6-DOF HID interfaces and opens new ones for
// passive reading. Owned/driven by DeviceManager (see ScanSpaceMice()).
#pragma once
#include "SpaceMouseDevice.h"
#include <memory>
#include <string>
#include <vector>

namespace InputBridge::SpaceMouse {

class SpaceMouseManager {
public:
    struct ScanResult {
        std::vector<std::unique_ptr<SpaceMouseDevice>> opened; // newly opened
        std::vector<std::string> present_paths;                // every matching path seen
    };

    // Opens every matching interface not in `already_open_paths`. Never
    // opens a path twice. An open that fails (e.g. another driver holds the
    // device exclusively) is logged once per path and retried on later scans;
    // it is never treated as fatal.
    static ScanResult Scan(const std::vector<std::string> &already_open_paths);
};

} // namespace InputBridge::SpaceMouse
