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

#if defined(__linux__)
    // True if the most recent Scan() found a 3Dconnexion interface it could
    // not open because of file permissions (EACCES on the hidraw node).
    // Cleared by the first Scan() in which no such failure occurs, so it
    // also goes away on its own once a udev rule is in place and the retry
    // succeeds. Safe to poll from the UI thread. Drives the "Fix
    // permissions" banner in the Devices tab.
    static bool HadRecentLinuxPermissionError();
#endif
};

} // namespace InputBridge::SpaceMouse
