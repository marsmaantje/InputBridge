#include "LinuxExclusiveMode.h"

#ifdef __linux__

#include <SDL3/SDL.h>
#include <filesystem>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

// ─── IsAvailable ──────────────────────────────────────────────────────────────

bool LinuxExclusiveMode::IsAvailable() const {
    return std::filesystem::exists("/dev/input");
}

// ─── FindAllInputDevicePaths ──────────────────────────────────────────────────
//
// Returns every /dev/input/eventN AND /dev/input/jsN node that belongs to the
// same physical device as `joystick`.
//
// Why both?
//   EVIOCGRAB on an eventN fd only blocks other *evdev* readers.  The joydev
//   kernel driver (/dev/input/jsN) is a completely separate input handler —
//   it registers directly with the input core and is unaffected by an evdev
//   EVIOCGRAB.  SDL (and therefore Resonite) will happily fall back to jsN if
//   the event node appears busy, so we must grab both.

std::vector<std::string> LinuxExclusiveMode::FindAllInputDevicePaths(SDL_Joystick* joystick) {
    std::vector<std::string> result;

    const char* sdlPath = SDL_GetJoystickPath(joystick);
    if (!sdlPath) {
        SDL_Log("LinuxExclusiveMode: cannot get joystick path.");
        return result;
    }
    SDL_Log("LinuxExclusiveMode: SDL reported path = %s", sdlPath);

    // Collect both event* and js* leaves from a sysfs input directory.
    auto collectInputNodes = [&](const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) return;
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            std::string fname = e.path().filename().string();
            // eventN  (/dev/input/eventN)
            if (fname.rfind("event", 0) == 0 && fname.size() > 5
                && std::isdigit(static_cast<unsigned char>(fname[5])))
            {
                result.push_back("/dev/input/" + fname);
                SDL_Log("LinuxExclusiveMode:  + evdev  %s", result.back().c_str());
            }
            // jsN  (/dev/input/jsN)
            else if (fname.rfind("js", 0) == 0 && fname.size() > 2
                     && std::isdigit(static_cast<unsigned char>(fname[2])))
            {
                result.push_back("/dev/input/" + fname);
                SDL_Log("LinuxExclusiveMode:  + joydev %s", result.back().c_str());
            }
        }
    };

    // Walk sysfs from an inputN directory to collect all sibling nodes.
    // inputDir should be something like /sys/class/input/input5
    auto collectFromInputN = [&](const std::filesystem::path& inputN) {
        if (!std::filesystem::exists(inputN)) return;
        for (const auto& e : std::filesystem::directory_iterator(inputN)) {
            std::string fname = e.path().filename().string();
            if (fname.rfind("event", 0) == 0 && fname.size() > 5
                && std::isdigit(static_cast<unsigned char>(fname[5])))
            {
                result.push_back("/dev/input/" + fname);
                SDL_Log("LinuxExclusiveMode:  + evdev  %s", result.back().c_str());
            }
            else if (fname.rfind("js", 0) == 0 && fname.size() > 2
                     && std::isdigit(static_cast<unsigned char>(fname[2])))
            {
                result.push_back("/dev/input/" + fname);
                SDL_Log("LinuxExclusiveMode:  + joydev %s", result.back().c_str());
            }
        }
    };

    // ── Case 1: hidraw path (/dev/hidrawN) ────────────────────────────────
    if (strncmp(sdlPath, "/dev/hidraw", 11) == 0) {
        int num = std::atoi(sdlPath + 11);
        char sysPath[256];
        snprintf(sysPath, sizeof(sysPath),
                 "/sys/class/hidraw/hidraw%d/device", num);
        try {
            auto devPath = std::filesystem::canonical(
                std::filesystem::path(sysPath).parent_path()
                / std::filesystem::read_symlink(sysPath));

            std::filesystem::path inputDir = devPath / "input";
            if (std::filesystem::exists(inputDir)) {
                for (const auto& inputEntry :
                     std::filesystem::directory_iterator(inputDir)) {
                    std::string n = inputEntry.path().filename().string();
                    if (n.rfind("input", 0) == 0 && n.size() > 5)
                        collectFromInputN(inputEntry.path());
                }
            }
            collectInputNodes(devPath);
        } catch (const std::exception& ex) {
            SDL_Log("LinuxExclusiveMode: hidraw sysfs walk error: %s", ex.what());
        }
    }
    // ── Case 2: legacy joydev path (/dev/input/jsN) ───────────────────────
    else if (strncmp(sdlPath, "/dev/input/js", 13) == 0) {
        // Add the js node itself first.
        result.push_back(sdlPath);
        SDL_Log("LinuxExclusiveMode:  + joydev %s", sdlPath);

        // Walk sysfs to find sibling eventN nodes.
        int num = std::atoi(sdlPath + 13);
        char sysPath[256];
        snprintf(sysPath, sizeof(sysPath),
                 "/sys/class/input/js%d/device", num);
        try {
            collectInputNodes(sysPath);
        } catch (...) {
            SDL_Log("LinuxExclusiveMode: js%d sysfs walk failed.", num);
        }
    }
    // ── Case 3: evdev path (/dev/input/eventN) ────────────────────────────
    else if (strncmp(sdlPath, "/dev/input/event", 16) == 0) {
        // Add the event node itself.
        result.push_back(sdlPath);
        SDL_Log("LinuxExclusiveMode:  + evdev  %s", sdlPath);

        // Walk sysfs backward to find sibling js* nodes.
        // /sys/class/input/eventN -> device -> input -> inputN -> js*, event*
        int num = std::atoi(sdlPath + 16);
        char sysEventPath[256];
        snprintf(sysEventPath, sizeof(sysEventPath),
                 "/sys/class/input/event%d/device", num);
        try {
            // sysEventPath resolves to e.g. /sys/devices/.../inputN
            auto inputN = std::filesystem::canonical(
                std::filesystem::path(sysEventPath).parent_path()
                / std::filesystem::read_symlink(sysEventPath));

            SDL_Log("LinuxExclusiveMode: resolved inputN = %s", inputN.c_str());
            collectFromInputN(inputN);
        } catch (const std::exception& ex) {
            SDL_Log("LinuxExclusiveMode: eventN sysfs walk error: %s", ex.what());
        }
    }

    // Deduplicate (different sysfs walks can surface the same node twice).
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}

// ─── HideDevice ───────────────────────────────────────────────────────────────

bool LinuxExclusiveMode::HideDevice(SDL_Joystick* joystick) {
    SDL_JoystickID id = SDL_GetJoystickID(joystick);
    if (m_GrabbedDevices.count(id)) return true; // already hidden

    auto paths = FindAllInputDevicePaths(joystick);
    if (paths.empty()) {
        SDL_Log("LinuxExclusiveMode: no input nodes found for '%s'.",
                SDL_GetJoystickName(joystick));
        return false;
    }

    std::vector<GrabEntry> grabbed;
    for (const auto& devPath : paths) {
        int fd = open(devPath.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            // O_RDWR may fail on js* nodes if we lack write permission; retry
            // read-only.  We only need the fd open so that joydev delivers events
            // to us (not other processes) once we also hold the evdev grab.
            fd = open(devPath.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                SDL_Log("LinuxExclusiveMode: open(%s) failed: %s",
                        devPath.c_str(), strerror(errno));
                continue;
            }
        }

        // EVIOCGRAB works on event* nodes.  For js* nodes the ioctl will return
        // ENOTTY/EINVAL because joydev doesn't implement it — that's fine, we
        // still hold the fd which prevents the node from going away and signals
        // the kernel that someone is already using it (some apps check for this).
        if (ioctl(fd, EVIOCGRAB, 1) < 0) {
            if (errno != ENOTTY && errno != EINVAL) {
                // Real error (e.g. EBUSY — another process already has an
                // exclusive grab).
                SDL_Log("LinuxExclusiveMode: EVIOCGRAB on %s failed: %s",
                        devPath.c_str(), strerror(errno));
            } else {
                SDL_Log("LinuxExclusiveMode: EVIOCGRAB not supported on %s "
                        "(joydev node — fd held open).", devPath.c_str());
            }
            // Keep the fd open regardless so we at least hold a reference.
        }

        grabbed.push_back({fd, devPath});
        SDL_Log("LinuxExclusiveMode: holding %s", devPath.c_str());
    }

    if (grabbed.empty()) return false;

    m_GrabbedDevices[id] = std::move(grabbed);
    SDL_Log("LinuxExclusiveMode: '%s' is now hidden (%zu node(s) grabbed).",
            SDL_GetJoystickName(joystick), m_GrabbedDevices[id].size());
    return true;
}

// ─── UnhideDevice ─────────────────────────────────────────────────────────────

bool LinuxExclusiveMode::UnhideDevice(SDL_Joystick* joystick) {
    SDL_JoystickID id = SDL_GetJoystickID(joystick);
    ReleaseInstance(id);
    SDL_Log("LinuxExclusiveMode: '%s' unhidden.", SDL_GetJoystickName(joystick));
    return true;
}

void LinuxExclusiveMode::ReleaseInstance(SDL_JoystickID id) {
    auto it = m_GrabbedDevices.find(id);
    if (it == m_GrabbedDevices.end()) return;

    for (auto& entry : it->second) {
        ioctl(entry.fd, EVIOCGRAB, 0); // no-op on js* fds but harmless
        close(entry.fd);
        SDL_Log("LinuxExclusiveMode: released %s", entry.path.c_str());
    }
    m_GrabbedDevices.erase(it);
}

// ─── Destructor ───────────────────────────────────────────────────────────────

LinuxExclusiveMode::~LinuxExclusiveMode() {
    for (auto& [id, entries] : m_GrabbedDevices) {
        for (auto& e : entries) {
            ioctl(e.fd, EVIOCGRAB, 0);
            close(e.fd);
        }
    }
    m_GrabbedDevices.clear();
}

#endif // __linux__