// src/Devices/Wiimote/Linux/WiimoteLinuxDiagnostics.cpp
#ifdef __linux__
#include "WiimoteLinuxDiagnostics.h"
#include "Devices/Wiimote/Linux/LinuxUdevInstaller.h"
#include "Devices/Wiimote/WiimoteProtocol.h"

#include <SDL3/SDL_hidapi.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace InputBridge::Wiimote {

namespace {

using Status = WiimoteLinuxDiagnostics::Status;
using CheckResult = WiimoteLinuxDiagnostics::CheckResult;

constexpr const char *kUdevRulesPath = "/etc/udev/rules.d/99-inputbridge-wiimote.rules";

bool FileExists(const std::string &path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// Generic "is <name> on $PATH" check - same technique as
// LinuxUdevInstaller::IsPkexecAvailable(), just not hardcoded to pkexec,
// since this file also needs to check for udevadm and bluetoothctl.
bool BinaryOnPath(const char *name) {
    const char *path_env = std::getenv("PATH");
    if (!path_env) return false;
    std::string path_copy(path_env);
    size_t start = 0;
    while (start <= path_copy.size()) {
        size_t end = path_copy.find(':', start);
        if (end == std::string::npos) end = path_copy.size();
        std::string dir = path_copy.substr(start, end - start);
        if (!dir.empty() && FileExists(dir + "/" + name)) return true;
        start = end + 1;
    }
    return false;
}

CheckResult CheckUdevRule() {
    if (FileExists(kUdevRulesPath)) {
        return {Status::Ok, "Permission rule",
                std::string("Installed at ") + kUdevRulesPath + "."};
    }
    return {Status::Warning, "Permission rule",
            "Not installed. Only needed if you connect a Wiimote/Balance "
            "Board through a USB Bluetooth dongle - a laptop's built-in "
            "adapter usually doesn't need it. Install it from Settings > "
            "Linux Permissions if you hit a permission-denied error."};
}

// Diagnostic-only open()/close() of any currently-enumerable Wiimote/
// Balance Board hidraw node - fd is closed immediately and never used for
// real I/O, so this can't interfere with WiimoteManager actually opening
// the device on its own next scan. Same technique as WiimoteManager.cpp's
// own LogLinuxOpenDiagnostics(), just returning a result instead of only
// logging one.
CheckResult CheckHidrawAccess() {
    SDL_hid_device_info *devs = SDL_hid_enumerate(kVendorNintendo, 0);
    int matched = 0;
    int openable = 0;
    std::string problems;
    for (auto d = devs; d; d = d->next) {
        if (d->product_id != kProductWiimote && d->product_id != kProductWiimotePlus)
            continue;
        ++matched;
        const std::string path = d->path ? d->path : "";
        if (path.empty()) continue;

        const int fd = ::open(path.c_str(), O_RDWR);
        const int open_errno = errno;
        if (fd >= 0) {
            ++openable;
            ::close(fd);
        } else {
            if (!problems.empty()) problems += " ";
            problems += path + ": " +
                (open_errno == EACCES ? std::string("permission denied (EACCES).")
                                       : "errno " + std::to_string(open_errno) + ".");
        }
    }
    SDL_hid_free_enumeration(devs);

    if (matched == 0) {
        return {Status::Info, "Device access",
                "No Wiimote/Balance Board currently detected - plug one in "
                "(or pair it) to test this check. Not a problem if none is "
                "attached right now."};
    }
    if (openable == matched) {
        return {Status::Ok, "Device access",
                std::to_string(matched) + " device(s) found, all openable."};
    }
    return {Status::Warning, "Device access",
            std::to_string(matched - openable) + " of " + std::to_string(matched) +
            " device(s) found couldn't be opened: " + problems};
}

// Checks whether 'plugdev' is active for THIS process right now (not just
// recorded in /etc/group), since that's what actually determines whether
// InputBridge can use a plugdev-gated hidraw node - being listed in
// /etc/group after install-udev-rules.sh adds you doesn't take effect
// until the next login, which is exactly the case this needs to catch.
CheckResult CheckPlugdevMembership() {
    struct group *plugdev = ::getgrnam("plugdev");
    if (!plugdev) {
        return {Status::Info, "'plugdev' group",
                "This system doesn't have a 'plugdev' group - it likely "
                "relies on udev's uaccess/logind ACL tagging instead, so "
                "this check doesn't apply here."};
    }
    const gid_t plugdev_gid = plugdev->gr_gid;

    if (::getgid() == plugdev_gid || ::getegid() == plugdev_gid) {
        return {Status::Ok, "'plugdev' group", "Active for this session."};
    }

    const int n = ::getgroups(0, nullptr);
    if (n > 0) {
        std::vector<gid_t> groups(static_cast<size_t>(n));
        if (::getgroups(n, groups.data()) == n) {
            for (gid_t g : groups) {
                if (g == plugdev_gid)
                    return {Status::Ok, "'plugdev' group", "Active for this session."};
            }
        }
    }

    return {Status::Warning, "'plugdev' group",
            "Not active for this session. If you were just added to "
            "'plugdev' (e.g. by installing the permission rule above), log "
            "out and back in - group membership changes don't apply to "
            "already-open sessions."};
}

CheckResult CheckPkexec() {
    if (InputBridge::Wiimote::LinuxUdevInstaller::IsPkexecAvailable()) {
        return {Status::Ok, "pkexec",
                "Available - the Install/Remove buttons in Settings > "
                "Linux Permissions can prompt for authentication directly."};
    }
    return {Status::Info, "pkexec",
            "Not found on PATH. The Install/Remove buttons in Settings "
            "won't work; run packaging/linux/install-udev-rules.sh "
            "manually with sudo instead."};
}

CheckResult CheckUdevadm() {
    if (BinaryOnPath("udevadm")) {
        return {Status::Ok, "udevadm",
                "Available - the permission rule can be applied without "
                "unplugging the device."};
    }
    return {Status::Warning, "udevadm",
            "Not found on PATH. After installing the permission rule "
            "you'll need to unplug and replug the device (or reboot) for "
            "it to take effect."};
}

CheckResult CheckBluetoothAdapter() {
    bool adapter_present = false;
    if (DIR *d = ::opendir("/sys/class/bluetooth")) {
        while (struct dirent *entry = ::readdir(d)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") {
                adapter_present = true;
                break;
            }
        }
        ::closedir(d);
    }
    const bool bluez_tools = BinaryOnPath("bluetoothctl");

    if (!adapter_present) {
        return {Status::Warning, "Bluetooth adapter",
                "No adapter detected under /sys/class/bluetooth. Needed to "
                "pair a Wiimote over Bluetooth - a USB Bluetooth dongle "
                "also works if your system doesn't have one built in."};
    }
    if (!bluez_tools) {
        return {Status::Info, "Bluetooth adapter",
                "Adapter detected, but 'bluetoothctl' (BlueZ) wasn't found "
                "on PATH - pairing a Wiimote over Bluetooth needs "
                "bluetoothd installed and running."};
    }
    return {Status::Ok, "Bluetooth adapter",
            "Adapter detected and BlueZ tools are available."};
}

// See src/Devices/Wiimote/README.md's "IR camera doesn't work while Steam
// is running" section - Steam Input's own HID polling resets the
// Wiimote's report mode at the firmware level regardless of which process
// asked for it, which can silently stop IR data from arriving even though
// nothing is wrong with InputBridge's own setup.
CheckResult CheckSteamRunning() {
    bool steam_running = false;
    if (DIR *d = ::opendir("/proc")) {
        while (struct dirent *entry = ::readdir(d)) {
            if (entry->d_type != DT_DIR) continue;
            const std::string name = entry->d_name;
            if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0])))
                continue;

            std::ifstream comm("/proc/" + name + "/comm");
            std::string line;
            if (comm && std::getline(comm, line) && line == "steam") {
                steam_running = true;
                break;
            }
        }
        ::closedir(d);
    }

    if (steam_running) {
        return {Status::Info, "Steam",
                "Currently running. Steam Input is known to intermittently "
                "reset a Wiimote's IR camera reporting mode - see the "
                "Wiimote module README's 'IR camera doesn't work while "
                "Steam is running' section if IR data drops out "
                "unexpectedly."};
    }
    return {Status::Ok, "Steam", "Not currently running."};
}

} // namespace

std::vector<WiimoteLinuxDiagnostics::CheckResult> WiimoteLinuxDiagnostics::RunAll() {
    return {
        CheckUdevRule(),
        CheckHidrawAccess(),
        CheckPlugdevMembership(),
        CheckPkexec(),
        CheckUdevadm(),
        CheckBluetoothAdapter(),
        CheckSteamRunning(),
    };
}

} // namespace InputBridge::Wiimote

#endif // __linux__
