// src/Devices/SpaceMouse/SpaceMouseManager.cpp
#include "SpaceMouseManager.h"
#include "App/Log.h"
#include <SDL3/SDL_hidapi.h>
#include <algorithm>
#include <atomic>
#include <set>
#include <string>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace InputBridge::SpaceMouse {

namespace {
constexpr const char *kTag = "SpaceMouseManager";

std::string WideToUtf8Lossy(const wchar_t *w) {
    std::string out;
    if (!w) return out;
    while (*w != L'\0' && out.size() < 128) {
        out.push_back(*w < 0x80 ? static_cast<char>(*w) : '?');
        ++w;
    }
    return out;
}

// Paths we've already reported a failed open for, so the periodic rescan
// doesn't repeat the same warning every few seconds.
std::set<std::string, std::less<>> &FailedPaths() {
    static std::set<std::string, std::less<>> s;
    return s;
}

#if defined(__linux__)
// Published by Scan(), read by HadRecentLinuxPermissionError() from the UI.
std::atomic<bool> s_had_recent_linux_permission_error{false};

// Read-only diagnostic open (fd closed immediately): returns the errno a
// plain open of the hidraw node gives, so a udev permission problem (EACCES)
// can be told apart from "busy"/"gone". 0 if the node opened fine.
int ProbeOpenErrno(const char *path) {
    const int fd = ::open(path, O_RDONLY | O_NONBLOCK);
    const int e = errno;
    if (fd >= 0) { ::close(fd); return 0; }
    return e;
}
#endif
} // namespace

SpaceMouseManager::ScanResult SpaceMouseManager::Scan(const std::vector<std::string> &already_open_paths) {
    ScanResult result;
    std::set<std::string, std::less<>> seen;
#if defined(__linux__)
    bool saw_permission_error = false;
#endif

    // Enumerate only the two 3Dconnexion vendor IDs - never the whole HID
    // tree - so unrelated devices are never even looked at.
    for (const uint16_t vid : {kVendorLogitech, kVendor3Dconnexion}) {
        SDL_hid_device_info *devs = SDL_hid_enumerate(vid, 0);
        for (auto d = devs; d; d = d->next) {
            if (!d->path) continue;
            const auto model = IdentifyDevice(d->vendor_id, d->product_id, d->usage_page, d->usage);
            if (!model) continue;

            const std::string path = d->path;
            result.present_paths.push_back(path);
            seen.insert(path);

            if (std::ranges::find(already_open_paths, path) != already_open_paths.end())
                continue;
            if (FailedPaths().contains(path)) {
                // Retry quietly; only the first failure is logged.
                if (SDL_hid_device *retry = SDL_hid_open_path(path.c_str())) {
                    FailedPaths().erase(path);
                    LOG_INFO(kTag, "Opened '%s' on retry", path.c_str());
                    result.opened.push_back(std::make_unique<SpaceMouseDevice>(
                        retry, path, WideToUtf8Lossy(d->serial_number), *model));
                }
#if defined(__linux__)
                else if (ProbeOpenErrno(path.c_str()) == EACCES) {
                    saw_permission_error = true; // keep the banner up
                }
#endif
                continue;
            }

            SDL_hid_device *hdev = SDL_hid_open_path(path.c_str());
            if (!hdev) {
                FailedPaths().insert(path);
#if defined(__linux__)
                const int err = ProbeOpenErrno(path.c_str());
                if (err == EACCES) {
                    saw_permission_error = true;
                    LOG_WARN(kTag, "Found %s (%04x:%04x) at '%s' but permission was denied. "
                                   "Use \"Fix permissions\" in the Devices tab, or install "
                                   "packaging/linux/udev/71-inputbridge-3dconnexion.rules. "
                                   "Will retry automatically.",
                             model->name, d->vendor_id, d->product_id, path.c_str());
                } else
#endif
                {
                    LOG_WARN(kTag, "Found %s (%04x:%04x) at '%s' but could not open it - "
                                   "another driver may hold it exclusively. Will retry quietly.",
                             model->name, d->vendor_id, d->product_id, path.c_str());
                }
                continue;
            }
            LOG_INFO(kTag, "Opened %s (%04x:%04x) read-only at '%s'", model->name,
                     d->vendor_id, d->product_id, path.c_str());
            result.opened.push_back(std::make_unique<SpaceMouseDevice>(
                hdev, path, WideToUtf8Lossy(d->serial_number), *model));
        }
        SDL_hid_free_enumeration(devs);
    }

    // Forget failures for paths that vanished so a re-plug logs afresh.
    for (auto it = FailedPaths().begin(); it != FailedPaths().end();) {
        it = seen.contains(*it) ? std::next(it) : FailedPaths().erase(it);
    }

#if defined(__linux__)
    s_had_recent_linux_permission_error = saw_permission_error;
#endif
    return result;
}

#if defined(__linux__)
bool SpaceMouseManager::HadRecentLinuxPermissionError() {
    return s_had_recent_linux_permission_error.load(std::memory_order::seq_cst);
}
#endif

} // namespace InputBridge::SpaceMouse
