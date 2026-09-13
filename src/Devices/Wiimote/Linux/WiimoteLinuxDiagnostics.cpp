// src/Devices/Wiimote/Linux/WiimoteLinuxDiagnostics.cpp
#ifdef __linux__
#include "WiimoteLinuxDiagnostics.h"
#include "Devices/Wiimote/Linux/LinuxUdevInstaller.h"
#include "Devices/Wiimote/WiimoteProtocol.h"

#include <SDL3/SDL_hidapi.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <grp.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace InputBridge::Wiimote {

namespace {

using Status = WiimoteLinuxDiagnostics::Status;
using CheckResult = WiimoteLinuxDiagnostics::CheckResult;

constexpr const char *kUdevRulesPath = "/etc/udev/rules.d/71-inputbridge-wiimote.rules";

// Filenames this rule has shipped under in past InputBridge versions,
// other than kUdevRulesPath above. install-udev-rules.sh cleans these up
// on install/uninstall (see migrate_away_from_legacy_filenames() there),
// but a user could still be sitting on one from before upgrading, or
// could have hand-copied the file into place under the old name from an
// old download - so this diagnostic checks for them directly rather
// than assuming the installer already ran.
//
// 99-inputbridge-wiimote.rules is the one that matters: earlier
// versions shipped under that name, and 99 sorts *after* systemd's own
// /usr/lib/udev/rules.d/73-seat-late.rules - the rule that actually
// applies the uaccess ACL grant based on whatever TAG state is already
// set by the time udev's single filename-sorted pass reaches priority
// 73. A TAG+="uaccess" first assigned at priority 99 is too late for
// that grant to see it: the tag still lands in the udev database (so
// `udevadm info` reports it correctly under CURRENT_TAGS) but the
// device is left root:root/0600 with no ACL and no error anywhere -
// confirmed the hard way against a real Balance Board connected over a
// Bluetooth dongle. See kUaccessGrantRulePriorityThreshold below.
constexpr std::array<const char *, 1> kLegacyUdevRulesPaths = {
    "/etc/udev/rules.d/99-inputbridge-wiimote.rules",
};

// systemd's uaccess-granting rule lives at this priority in
// /usr/lib/udev/rules.d/73-seat-late.rules. Any rule file that assigns
// TAG+="uaccess" for the first time at this priority or higher is too
// late in udev's single sorted pass for that grant to apply - see the
// comment on kLegacyUdevRulesPaths above for the full mechanism. This
// is a systemd implementation detail, not something InputBridge
// controls, so it's plausible (if unlikely) it moves in a future
// systemd release; if diagnostics start reporting false positives/
// negatives here after a systemd update, check
// /usr/lib/udev/rules.d/*seat-late* for the current priority.
constexpr int kUaccessGrantRulePriorityThreshold = 73;

// Parses the leading run of ASCII digits from a rules file's basename
// (e.g. "71" from "71-inputbridge-wiimote.rules"), the way udev itself
// treats the numeric prefix convention. Returns -1 if the filename
// doesn't start with a digit at all, since such a file sorts after
// every purely-numeric-prefixed one anyway (ASCII digits < most other
// characters udev rule files use), which for our purposes here means
// "can't tell, don't claim it's safe."
int ParseRulePriorityPrefix(const std::string &basename) {
    size_t i = 0;
    while (i < basename.size() && std::isdigit(static_cast<unsigned char>(basename[i]))) ++i;
    if (i == 0) return -1;
    return std::atoi(basename.substr(0, i).c_str());
}

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

// Reads the installed rules file and flags a known-bad pattern: a single
// rule line combining TAG+="uaccess" with GROUP="plugdev" (an early
// version of the shipped rules file did exactly this). On systems with
// no "plugdev" group, systemd-udevd fails to resolve that GROUP= and -
// confirmed via `udevadm test` - invalidates the ENTIRE line, silently
// dropping the TAG+="uaccess" grant along with it. Splitting the two
// assignments into separate lines (see the current rules file) avoids
// this, but there's no way to tell from here whether a *running*
// system's udevd actually has this bug (it varies by version), so this
// only warns when it can see the risky pattern still present on disk -
// it can't detect the failure mode itself without shelling out to
// `udevadm test`, which this diagnostic deliberately avoids doing.
bool RulesFileHasCombinedUaccessGroupLine(const std::string &path) {
    std::ifstream file(path);
    if (!file) return false;
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments (and blank/whitespace-only lines) before scanning -
        // without this, the doc-comment above explaining this exact bug
        // (which mentions both "TAG+=\"uaccess\"" and "GROUP=" in prose)
        // trips its own check.
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;

        if (line.find("TAG+=\"uaccess\"") != std::string::npos &&
            line.find("GROUP=") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Finds every legacy-named copy of our rule that's actually present on
// disk right now (there's normally at most one, but nothing stops a
// user from having several stale copies from different old versions).
std::vector<std::string> FindPresentLegacyRuleFiles() {
    std::vector<std::string> present;
    for (const char *path : kLegacyUdevRulesPaths) {
        if (FileExists(path)) present.emplace_back(path);
    }
    return present;
}

// basename() without pulling in <libgen.h>'s mutating C version - just
// the piece after the last '/', which is all ParseRulePriorityPrefix()
// needs.
std::string PathBasename(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string JoinPaths(const std::vector<std::string> &paths) {
    std::string joined;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) joined += ", ";
        joined += paths[i];
    }
    return joined;
}

CheckResult CheckUdevRule() {
    const std::vector<std::string> legacy_present = FindPresentLegacyRuleFiles();
    const bool current_present = FileExists(kUdevRulesPath);

    // Stale copies under an old filename are worth flagging even when
    // the current one is also installed and working: at best they're
    // dead weight that makes `ls /etc/udev/rules.d` and future
    // debugging more confusing than it needs to be; at worst (if
    // they're the only copy - see the branch below) they're silently
    // not granting the access they appear to.
    std::string legacy_note;
    if (!legacy_present.empty()) {
        legacy_note = " Also found " + std::to_string(legacy_present.size()) +
            " leftover rule file(s) from an older InputBridge version at a "
            "priority (>= " + std::to_string(kUaccessGrantRulePriorityThreshold) +
            ") too late for systemd's uaccess ACL grant to reliably apply: " +
            JoinPaths(legacy_present) +
            ". Reinstall from Settings > Linux Permissions to remove "
            "these automatically, or delete them manually.";
    }

    if (current_present) {
        const int priority = ParseRulePriorityPrefix(PathBasename(kUdevRulesPath));
        if (priority < 0 || priority >= kUaccessGrantRulePriorityThreshold) {
            // Shouldn't happen given the constant above, but if
            // kUdevRulesPath and kUaccessGrantRulePriorityThreshold
            // ever drift out of sync (or someone edits one without the
            // other), this is exactly the bug that cost real debugging
            // time before - surface it loudly rather than silently
            // reporting Ok for a rule whose ACL grant may not apply.
            return {Status::Warning, "Permission rule",
                    std::string("Installed at ") + kUdevRulesPath +
                    ", but its filename priority doesn't sort before systemd's "
                    "own uaccess-granting rule (priority " +
                    std::to_string(kUaccessGrantRulePriorityThreshold) +
                    "). The tag will show up in `udevadm info` but the actual "
                    "ACL grant may silently never apply. This shouldn't happen "
                    "with a stock InputBridge install - please report it." + legacy_note};
        }
        if (RulesFileHasCombinedUaccessGroupLine(kUdevRulesPath)) {
            return {Status::Warning, "Permission rule",
                    std::string("Installed at ") + kUdevRulesPath +
                    ", but at least one rule line combines TAG+=\"uaccess\" "
                    "with GROUP=\"plugdev\" on the same line. On a system "
                    "with no 'plugdev' group, some udevd versions fail the "
                    "*entire* line when GROUP can't be resolved - silently "
                    "dropping the uaccess grant too, even though Device "
                    "access below reports EACCES. Reinstall the current "
                    "permission rule from Settings > Linux Permissions to "
                    "get the fixed version, which splits these onto "
                    "separate lines." + legacy_note};
        }
        return {Status::Ok, "Permission rule",
                std::string("Installed at ") + kUdevRulesPath + "." + legacy_note};
    }

    if (!legacy_present.empty()) {
        // The only copy on disk is one that can't actually grant
        // access - this looks installed (a file is there, `udevadm
        // info` will show the tag) but silently doesn't work. Worth a
        // distinctly stronger message than the generic "not installed"
        // case below, since a user hitting this has already tried to
        // fix it and reasonably believes it's done.
        return {Status::Warning, "Permission rule",
                std::string("No rule found at the current expected location (") +
                kUdevRulesPath + "), but found " + std::to_string(legacy_present.size()) +
                " file(s) installed under an old, too-late priority that cannot "
                "grant the uaccess ACL (systemd's own grant at priority " +
                std::to_string(kUaccessGrantRulePriorityThreshold) +
                " already runs before it takes effect): " + JoinPaths(legacy_present) +
                ". Reinstall from Settings > Linux Permissions to replace it with "
                "the current, correctly-numbered rule."};
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
