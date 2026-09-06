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

// "/dev/hidraw3" -> "hidraw3". Defensive about a bare "hidraw3" too (in
// case a future hidapi version changes convention): takes whatever's
// after the last '/', or the whole string if there isn't one.
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
        LOG_VERBOSE(kTag, "Could not open %s - not a hidraw device, or sysfs layout differs "
                           "(falling back to hidapi/hidraw transport for %s)",
                    uevent_path.c_str(), hidraw_path.c_str());
        return std::nullopt;
    }

    std::string line;
    while (std::getline(f, line)) {
        constexpr const char *kPrefix = "HID_UNIQ=";
        constexpr size_t kPrefixLen = 9; // strlen("HID_UNIQ=")
        if (line.rfind(kPrefix, 0) != 0) continue;
        std::string value = line.substr(kPrefixLen);
        // Empty HID_UNIQ (e.g. a USB Wiimote receiver) just means "no
        // Bluetooth address here" - not an error.
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
    // `address` is already validated by ParseBluetoothAddress() (fixed
    // "AA:BB:...:FF" shape, no shell metacharacters) in every caller, so
    // this isn't building a shell command from unsanitized input.
    const std::string cmd = "bluetoothctl disconnect " + address + " >/dev/null 2>&1";
    LOG_INFO(kTag, "Asking BlueZ to release its HID connection to %s so a direct L2CAP "
                    "connection can take over (see WiimoteL2CAPTransport.h)", address.c_str());
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        LOG_WARN(kTag, "`bluetoothctl disconnect %s` returned %d (missing, already "
                        "disconnected, or other issue - non-fatal; the L2CAP connect "
                        "attempt will fail with its own log line if this didn't work)",
                 address.c_str(), rc);
    }
}

} // namespace InputBridge::Wiimote

#endif // __linux__
