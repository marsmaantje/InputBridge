// src/Devices/Wiimote/Linux/WiimoteBluetoothUtil.cpp
#ifdef __linux__
#include "WiimoteBluetoothUtil.h"
#include "App/Log.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "WiimoteBluetoothUtil";

// "/dev/hidraw3" -> "hidraw3". SDL_hid_device_info::path on Linux is
// always the /dev node, but be defensive about a bare "hidraw3" too
// (e.g. if a future hidapi version changes its convention) by just
// taking whatever's after the last '/', or the whole string if there
// isn't one.
std::string HidrawNodeName(const std::string &hidraw_path) {
    const size_t slash = hidraw_path.find_last_of('/');
    return slash == std::string::npos ? hidraw_path : hidraw_path.substr(slash + 1);
}
} // namespace

std::optional<std::array<uint8_t, 6>> ParseBluetoothAddress(const std::string &text) {
    unsigned b[6];
    // "AA:BB:CC:DD:EE:FF" - exactly 17 chars, 6 hex pairs separated by ':'.
    if (text.size() != 17) return std::nullopt;
    const int n = std::sscanf(text.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x",
                               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    if (n != 6) return std::nullopt;
    std::array<uint8_t, 6> out{};
    for (int i = 0; i < 6; ++i) out[i] = uint8_t(b[i]);
    return out;
}

std::optional<std::string> ResolveBluetoothAddressForHidrawPath(const std::string &hidraw_path) {
    const std::string node = HidrawNodeName(hidraw_path);
    if (node.empty()) return std::nullopt;

    const std::string uevent_path = "/sys/class/hidraw/" + node + "/device/uevent";
    std::ifstream f(uevent_path);
    if (!f) {
        LOG_VERBOSE(kTag, "Could not open %s (not a hidraw device, or sysfs layout differs on "
                           "this kernel) - falling back to the hidapi/hidraw transport for %s",
                    uevent_path.c_str(), hidraw_path.c_str());
        return std::nullopt;
    }

    std::string line;
    while (std::getline(f, line)) {
        constexpr const char *kPrefix = "HID_UNIQ=";
        constexpr size_t kPrefixLen = 9; // strlen("HID_UNIQ=")
        if (line.rfind(kPrefix, 0) != 0) continue;
        std::string value = line.substr(kPrefixLen);
        // A non-Bluetooth hidraw device (e.g. a USB Wiimote receiver, or
        // most other USB HID devices generally) reports an empty HID_UNIQ
        // - that's expected and just means "no Bluetooth address to
        // resolve here", not an error.
        if (value.empty()) return std::nullopt;
        if (!ParseBluetoothAddress(value)) {
            LOG_VERBOSE(kTag, "%s HID_UNIQ='%s' doesn't look like a Bluetooth address",
                        uevent_path.c_str(), value.c_str());
            return std::nullopt;
        }
        return value;
    }
    return std::nullopt;
}

void DisconnectExistingHidConnection(const std::string &address) {
    // popen()'s command string goes through /bin/sh -c - `address` was
    // already validated by ParseBluetoothAddress() (fixed "AA:BB:...:FF"
    // shape, no shell metacharacters possible) by every caller in this
    // module before reaching here, so this isn't building a shell command
    // out of unsanitized external input.
    const std::string cmd = "bluetoothctl disconnect " + address + " >/dev/null 2>&1";
    LOG_INFO(kTag, "Asking BlueZ to release its own HID connection to %s so a direct L2CAP "
                    "connection can take over (see WiimoteL2CAPTransport.h)", address.c_str());
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        LOG_WARN(kTag, "`bluetoothctl disconnect %s` returned %d (bluetoothctl missing, device "
                        "already disconnected, or something else - non-fatal, the subsequent "
                        "L2CAP connect attempt will just fail with its own clear log line if "
                        "this didn't actually free the channels)", address.c_str(), rc);
    }
}

} // namespace InputBridge::Wiimote

#endif // __linux__
