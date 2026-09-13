// src/Devices/Wiimote/Linux/WiimoteLinuxDiagnostics.h
//
// Read-only checks for the handful of Linux-specific things that most
// commonly break Wiimote/Balance Board support, surfaced from a "Check for
// common issues" button (see SettingsPanel.cpp) rather than making the
// user hunt through the log, packaging/linux/README.md, or this module's
// own README.md's "known gaps" section to piece it together themselves.
#pragma once
#ifdef __linux__

#include <string>
#include <vector>

namespace InputBridge::Wiimote {

// Every check here is a cheap, synchronous, side-effect-free read
// (stat()/getgrnam()/a PATH search/a diagnostic-only open+close/a procfs
// scan) - nothing here prompts for authentication or changes any system
// state, unlike LinuxUdevInstaller. That's why RunAll() is safe to call
// directly from the UI thread on a button click rather than needing the
// std::async/poll dance LinuxUdevInstaller's callers use.
class WiimoteLinuxDiagnostics {
public:
    enum class Status {
        Ok,       // known-good state
        Info,     // neutral/contextual - not necessarily a problem
        Warning,  // likely to be causing, or about to cause, a problem
    };

    struct CheckResult {
        Status status = Status::Info;
        std::string title;   // short name of what was checked
        std::string detail;  // human-readable explanation of the current state
    };

    // Runs every check and returns one result per check, in a fixed,
    // human-sensible order (permissions, then group membership, then the
    // tooling those depend on, then Bluetooth, then the Steam IR
    // conflict). Re-run on demand (e.g. after installing the permission
    // rule or replugging a device) rather than cached - nothing here is
    // expensive enough to need caching.
    static std::vector<CheckResult> RunAll();
};

} // namespace InputBridge::Wiimote

#endif // __linux__
