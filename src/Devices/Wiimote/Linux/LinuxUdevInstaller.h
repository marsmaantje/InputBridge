#pragma once
#ifdef __linux__

#include <string>

namespace InputBridge::Wiimote {

// Runs packaging/linux/install-udev-rules.sh as root via `pkexec`, so the
// user can fix the "Wiimote/Balance Board hidraw permission denied" case
// (see WiimoteManager::HadRecentLinuxPermissionError()) from a button in
// the UI instead of needing a terminal.
//
// pkexec (not sudo) because it's the standard desktop-facing mechanism for
// this: it shows a native polkit auth dialog appropriate to whatever
// desktop environment is running (GNOME/KDE/etc. all ship a polkit
// authentication agent), rather than requiring a terminal to type a
// password into, or InputBridge trying to build its own password prompt.
// It's present by default on essentially every mainstream desktop Linux
// distro.
class LinuxUdevInstaller {
public:
    enum class Result {
        Success,          // pkexec ran the script and it exited 0.
        UserCancelled,    // Auth dialog dismissed/declined (pkexec exit 126/127).
        ScriptNotFound,   // Couldn't locate install-udev-rules.sh next to the binary.
        PkexecNotFound,   // No pkexec available on this system.
        Failed,           // pkexec ran the script but it exited non-zero.
    };

    struct RunOutcome {
        Result result = Result::Failed;
        int exit_code = -1;
        std::string script_path;  // What we tried to run, for diagnostics/logging.
        std::string stderr_tail;  // Last bit of stderr, if any, for error messages.
    };

    // Locates install-udev-rules.sh relative to the running binary (see
    // ResolveScriptPath()) and runs it under pkexec, blocking until it
    // completes. Intended to be called from a background thread (e.g. a
    // std::async / std::thread kicked off by a UI button), NOT the UI
    // thread directly, since pkexec blocks on user interaction with the
    // auth dialog for as long as the user takes to respond.
    static RunOutcome InstallRules();

    // Same, but runs "install-udev-rules.sh --uninstall".
    static RunOutcome UninstallRules();

    // True if a pkexec binary is on PATH. Cheap to call from the UI thread
    // to decide whether to show the "Fix permissions" button at all versus
    // falling back to just printing the manual command.
    static bool IsPkexecAvailable();

    // Finds install-udev-rules.sh next to the running binary, checking
    // both the dev-build layout (share/inputbridge/udev/ directly beside
    // the executable) and the installed-package layout
    // (../share/inputbridge/udev/, since the binary lives in bin/). Empty
    // string if neither exists.
    static std::string ResolveScriptPath();

private:
    static RunOutcome RunScript(const std::string &script_path, const char *arg);
};

} // namespace InputBridge::Wiimote

#endif // __linux__
