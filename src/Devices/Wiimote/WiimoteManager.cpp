// src/Devices/Wiimote/WiimoteManager.cpp
#include "WiimoteManager.h"
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
#endif

namespace InputBridge::Wiimote {

bool WiimoteManager::IsWiimoteProductString(const char *product) {
    if (!product) return false;
    // "Nintendo RVL-CNT-01" (Wiimote), "Nintendo RVL-CNT-01-TR" (Wiimote
    // Plus), "Nintendo RVL-WBC-01" (Balance Board) - see WiiBrew's SDP table.
    return std::strstr(product, "RVL-CNT-01") != nullptr ||
           std::strstr(product, "RVL-WBC-01") != nullptr;
}

namespace {
// hidapi's own issue tracker documents open() racing enumerate() on Linux
// hidraw (permission bits/uaccess tagging can land microseconds after the
// device node shows up in a udev-driven enumerate, so the very next open()
// can transiently see stale permissions) - the workaround recommended
// there, absent a hard "wait for udev" signal, is a short bounded retry
// rather than a single attempt. A handful of tries a few ms apart costs
// nothing on the common case (first try succeeds) and turns what would
// otherwise be an up-to-kWiimoteScanIntervalMs-long outage into a
// sub-50ms one.
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
// SDL_hid_open_path() doesn't surface the underlying errno, so when it
// fails we can't tell "wrong permissions" (EACCES - fixable with a udev
// rule) apart from "something else already has it open in a way that
// matters" (EBUSY - unusual for hidraw, which the kernel documents as a
// non-exclusive raw-report tap that normally tolerates multiple readers)
// apart from "the node is gone" (ENOENT - a stale enumerate() result) from
// on-device diagnostics alone. Probe the raw path directly with a plain
// open()/close() so the log tells the actual story instead of us guessing
// at it - this is diagnostic-only, the fd is closed immediately and never
// used for real I/O (WiimoteDevice always goes through the SDL_hid_device
// OpenWithRetry() already obtained, or failed to).
void LogLinuxOpenDiagnostics(const char *path) {
    struct stat st{};
    const bool stat_ok = ::stat(path, &st) == 0;
    const int fd = ::open(path, O_RDWR);
    const int open_errno = errno;
    if (fd >= 0) {
        // Genuinely openable via a plain open() even though hidapi's own
        // open (which does extra ioctl/HIDIOCGRDESC work on top of the
        // same open()) failed - points at something hidapi-specific
        // rather than a plain permissions/exclusivity problem.
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
} // namespace

std::vector<std::unique_ptr<WiimoteDevice>> WiimoteManager::Scan(
    const std::vector<std::string> &already_open_paths) {
    std::vector<std::unique_ptr<WiimoteDevice>> out;

    SDL_hid_device_info *devs = SDL_hid_enumerate(kVendorNintendo, 0);
    for (auto d = devs; d; d = d->next) {
        if (d->product_id != kProductWiimote && d->product_id != kProductWiimotePlus)
            continue;

        const std::string path = d->path ? d->path : "";
        const bool already_tracked = !path.empty() &&
            std::find(already_open_paths.begin(), already_open_paths.end(), path) != already_open_paths.end();
        if (already_tracked)
            continue; // see the Init()-races-Init() hazard documented in the header

        SDL_hid_device *hdev = OpenWithRetry(d->path);
        if (!hdev) {
            // Most commonly at this point (after OpenWithRetry's own short
            // retries already ruled out the transient permission-race case
            // documented above): SDL's OWN joystick subsystem still holds
            // this HID node open at the platform layer, even though
            // SDL_HINT_JOYSTICK_HIDAPI_WII is disabled and DeviceManager
            // never calls SDL_OpenJoystick() on it - on some SDL/platform
            // combinations (observed with SDL3's Linux hidraw joystick
            // backend), SDL opens the underlying node just to enumerate
            // capabilities (VID/PID/name) for the SDL_EVENT_JOYSTICK_ADDED
            // callback. DeviceManager now scans again immediately after it
            // closes any such handle (see HandleDeviceAdded), so this is
            // expected to clear within one more scan rather than needing
            // the full kWiimoteScanIntervalMs periodic retry.
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

        auto device = std::make_unique<WiimoteDevice>(hdev, path, is_balance_board);
        if (!device->Init()) {
            // Keep it anyway - Init() partially failing (e.g. one register
            // write dropped) shouldn't hide the device; Poll() will keep
            // trying to make sense of whatever reports do arrive.
        }
        out.push_back(std::move(device));
    }
    SDL_hid_free_enumeration(devs);

    return out;
}

} // namespace InputBridge::Wiimote
