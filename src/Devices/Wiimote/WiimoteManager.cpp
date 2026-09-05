// src/Devices/Wiimote/WiimoteManager.cpp
#include "WiimoteManager.h"
#include "WiimoteHidTransport.h"
#include "App/Log.h"
#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_error.h>
#include <algorithm>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sys/stat.h>
#include "Linux/WiimoteL2CAPTransport.h"
#include "Linux/WiimoteBluetoothUtil.h"
#endif

namespace InputBridge::Wiimote {

bool WiimoteManager::IsWiimoteProductString(const char *product) {
    if (!product) return false;
    // "Nintendo RVL-CNT-01" (Wiimote), "Nintendo RVL-CNT-01-TR" (Wiimote
    // Plus), "Nintendo RVL-WBC-01" (Balance Board) - WiiBrew's SDP table.
    return std::strstr(product, "RVL-CNT-01") != nullptr ||
           std::strstr(product, "RVL-WBC-01") != nullptr;
}

namespace {
// hidapi's issue tracker documents open() racing enumerate() on Linux
// hidraw (permission tagging can land just after the node appears), so a
// short bounded retry clears the transient case cheaply.
SDL_hid_device *OpenWithRetry(const char *path) {
    constexpr int kMaxAttempts = 5;
    constexpr Uint32 kDelayMs = 10;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (attempt > 0) SDL_Delay(kDelayMs);
        if (SDL_hid_device *hdev = SDL_hid_open_path(path))
            return hdev;
    }
    return nullptr;
}

#if defined(__linux__)
// SDL_hid_open_path() doesn't surface errno, so diagnose with a plain
// open()/close() (diagnostic only - fd closed immediately, never used for
// real I/O) to tell EACCES (permissions/udev rule), EBUSY (something else
// has it open exclusively - unusual for hidraw), and ENOENT (stale
// enumerate() result) apart in the logs.
void LogLinuxOpenDiagnostics(const char *path) {
    struct stat st{};
    const bool stat_ok = ::stat(path, &st) == 0;
    const int fd = ::open(path, O_RDWR);
    const int open_errno = errno;
    if (fd >= 0) {
        LOG_WARN("WiimoteManager", "  diagnostic: plain open() of '%s' SUCCEEDED "
                 "(mode=0%o) even though SDL_hid_open_path() failed - likely an "
                 "hidapi-internal ioctl failure, not a permissions/exclusivity issue",
                 path, stat_ok ? (st.st_mode & 0777) : 0);
        ::close(fd);
        return;
    }
    const char *meaning =
        (open_errno == EACCES) ? "permission denied - check the hidraw udev rule/group for this device" :
        (open_errno == EBUSY)  ? "device busy - something else has it open with an exclusivity flag "
                                  "hidraw doesn't normally require" :
        (open_errno == ENOENT) ? "no such device - node disappeared between enumerate() and open() "
                                  "(likely a reconnect/rebind in progress)" :
                                  "see errno";
    LOG_WARN("WiimoteManager", "  diagnostic: plain open() of '%s' failed too: errno=%d (%s) - %s",
             path, open_errno, std::strerror(open_errno), meaning);
    if (stat_ok) {
        LOG_WARN("WiimoteManager", "  diagnostic: node mode=0%o owner_uid=%u owner_gid=%u",
                 st.st_mode & 0777, (unsigned)st.st_uid, (unsigned)st.st_gid);
    } else {
        LOG_WARN("WiimoteManager", "  diagnostic: stat() on '%s' also failed - node likely doesn't exist", path);
    }
}
#endif

#if defined(__linux__)
// Best-effort, PASSIVE ONLY: opens a direct L2CAP transport for the
// Wiimote at `hidraw_path` so it stops sharing the OS's generic HID node
// (and stops being reset by anything else that opens it, notably Steam
// Input - see README.md and WiimoteL2CAPTransport.h).
//
// Makes exactly ONE connect() attempt and never disconnects the OS's
// existing HID connection. An earlier version did that on failure, which
// tore down working Bluetooth links on already-connected Wiimotes (the
// common case) without a reliable reconnect - some Wiimotes need the sync
// button pressed again afterward - and could race SDL's joystick-added
// filter into leaving dead "Wii Remote (Gamepad)" entries. A user-
// initiated "take over this connection" action would be reasonable to add
// later, but this must stay non-automatic.
//
// Returns nullptr if this isn't Bluetooth (e.g. a USB receiver) or if the
// OS already holds the HID connection - callers fall back to
// WiimoteHidTransport as usual.
std::unique_ptr<WiimoteL2CAPTransport> TryOpenLinuxL2CAPTransport(const std::string &hidraw_path) {
    const auto address_str = ResolveBluetoothAddressForHidrawPath(hidraw_path);
    if (!address_str) return nullptr; // not a Bluetooth device

    const auto bdaddr = ParseBluetoothAddress(*address_str);
    if (!bdaddr) return nullptr; // shouldn't happen; already validated above

    if (auto transport = WiimoteL2CAPTransport::Connect(*bdaddr)) {
        LOG_INFO("WiimoteManager", "Opened direct L2CAP transport for %s (%s) - no competing OS "
                                    "HID connection was in the way",
                 hidraw_path.c_str(), address_str->c_str());
        return transport;
    }

    LOG_VERBOSE("WiimoteManager", "%s (%s) is already connected via the OS's own Bluetooth HID "
                                   "service - using the shared hidraw transport for this device "
                                   "(as always; this is expected and not an error)",
                hidraw_path.c_str(), address_str->c_str());
    return nullptr;
}
#endif // __linux__

} // namespace

std::vector<std::unique_ptr<WiimoteDevice>> WiimoteManager::Scan(
    const std::vector<std::string> &already_open_paths, bool try_linux_l2cap) {
    std::vector<std::unique_ptr<WiimoteDevice>> out;

    SDL_hid_device_info *devs = SDL_hid_enumerate(kVendorNintendo, 0);
    for (auto d = devs; d; d = d->next) {
        if (d->product_id != kProductWiimote && d->product_id != kProductWiimotePlus)
            continue;

        const std::string path = d->path ? d->path : "";
        const bool already_tracked = !path.empty() &&
            std::find(already_open_paths.begin(), already_open_paths.end(), path) != already_open_paths.end();
        if (already_tracked)
            continue; // see the concurrent-handle hazard documented in the header

        SDL_hid_device *hdev = OpenWithRetry(d->path);
        if (!hdev) {
            // Most commonly: SDL's own joystick subsystem still holds this
            // node open at the platform layer just to enumerate
            // capabilities for SDL_EVENT_JOYSTICK_ADDED, even with
            // SDL_HINT_JOYSTICK_HIDAPI_WII disabled. DeviceManager rescans
            // immediately after closing any such handle, so this should
            // clear within one more scan.
            LOG_WARN("WiimoteManager", "Found Wiimote-like HID device at '%s' but could not "
                     "open it after retrying (likely still held by another process/backend - "
                     "will retry again on the next scan)", path.c_str());
            const char *sdl_err = SDL_GetError();
            if (sdl_err && sdl_err[0]) {
                LOG_WARN("WiimoteManager", "  SDL_GetError(): %s", sdl_err);
            }
#if defined(__linux__)
            LogLinuxOpenDiagnostics(path.c_str());
#endif
            continue;
        }

        // wchar_t* product_string from hidapi; convert defensively.
        bool is_balance_board = false;
        char product_utf8[64] = {};
        if (d->product_string) {
            size_t i = 0;
            for (; d->product_string[i] && i < sizeof(product_utf8) - 1; ++i)
                product_utf8[i] = char(d->product_string[i]);
            is_balance_board = IsWiimoteProductString(product_utf8) &&
                                std::strstr(product_utf8, "WBC") != nullptr;
        }

        // WiimoteDevice's constructor defers Init() to its first Poll()
        // call (once its settle timer elapses), so a Wiimote connecting
        // mid-session gets the same settle time as one already on at
        // startup - see WiimoteDevice.h's m_InitSettleAtMs comment.
        std::unique_ptr<IWiimoteTransport> transport;
#if defined(__linux__)
        // Off by default - see try_linux_l2cap's doc comment in
        // WiimoteManager.h. `hdev` becomes redundant the moment this
        // succeeds, since all I/O moves to the new transport - close it
        // rather than leaking the hidraw handle.
        if (try_linux_l2cap) {
            if (auto l2cap = TryOpenLinuxL2CAPTransport(path)) {
                transport = std::move(l2cap);
                SDL_hid_close(hdev);
            }
        }
#endif
        if (!transport) transport = std::make_unique<WiimoteHidTransport>(hdev);

        out.push_back(std::make_unique<WiimoteDevice>(std::move(transport), path, is_balance_board));
    }
    SDL_hid_free_enumeration(devs);

    return out;
}

} // namespace InputBridge::Wiimote
