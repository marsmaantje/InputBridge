// src/Devices/SpaceMouse/SpaceMouseManager.cpp
#include "SpaceMouseManager.h"
#include "App/Log.h"
#include <SDL3/SDL_hidapi.h>
#include <algorithm>
#include <set>
#include <string>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace InputBridge::SpaceMouse {

namespace {
constexpr const char *kTag = "SpaceMouseManager";

std::string WideToUtf8Lossy(const wchar_t *w) {
    std::string out;
    if (!w) return out;
    for (; *w && out.size() < 128; ++w)
        out.push_back(*w < 0x80 ? static_cast<char>(*w) : '?');
    return out;
}

// Paths we've already reported a failed open for, so the periodic rescan
// doesn't repeat the same warning every few seconds.
std::set<std::string> &FailedPaths() {
    static std::set<std::string> s;
    return s;
}

#if defined(__linux__)
// Read-only diagnostic, fd closed immediately. Tells a udev permission
// problem apart from "busy"/"gone".
void LogLinuxOpenHint(const char *path) {
    const int fd = ::open(path, O_RDONLY | O_NONBLOCK);
    const int e = errno;
    if (fd >= 0) { ::close(fd); return; }
    if (e == EACCES) {
        LOG_WARN(kTag, "  permission denied opening '%s'. Install the 3Dconnexion udev rule "
                       "(packaging/linux/udev/71-inputbridge-3dconnexion.rules) and replug the device.",
                 path);
    } else {
        LOG_WARN(kTag, "  open() errno=%d (%s)", e, std::strerror(e));
    }
}
#endif
} // namespace

SpaceMouseManager::ScanResult SpaceMouseManager::Scan(const std::vector<std::string> &already_open_paths) {
    ScanResult result;
    std::set<std::string> seen;

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

            if (std::find(already_open_paths.begin(), already_open_paths.end(), path)
                != already_open_paths.end())
                continue;
            if (FailedPaths().count(path)) {
                // Retry quietly; only the first failure is logged.
                if (SDL_hid_device *retry = SDL_hid_open_path(path.c_str())) {
                    FailedPaths().erase(path);
                    LOG_INFO(kTag, "Opened '%s' on retry", path.c_str());
                    result.opened.push_back(std::make_unique<SpaceMouseDevice>(
                        retry, path, WideToUtf8Lossy(d->serial_number), *model));
                }
                continue;
            }

            SDL_hid_device *hdev = SDL_hid_open_path(path.c_str());
            if (!hdev) {
                FailedPaths().insert(path);
                LOG_WARN(kTag, "Found %s (%04x:%04x) at '%s' but could not open it - "
                               "another driver may hold it exclusively. Will retry quietly.",
                         model->name, d->vendor_id, d->product_id, path.c_str());
#if defined(__linux__)
                LogLinuxOpenHint(path.c_str());
#endif
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
    for (auto it = FailedPaths().begin(); it != FailedPaths().end();)
        it = seen.count(*it) ? std::next(it) : FailedPaths().erase(it);

    return result;
}

} // namespace InputBridge::SpaceMouse
