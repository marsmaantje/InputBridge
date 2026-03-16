#pragma once

#ifdef __linux__

#include "InputExclusiveModeImpl.h"
#include <string>
#include <vector>
#include <map>
#include <algorithm>

// Linux backend: EVIOCGRAB exclusive evdev grab, per device instance.
//
// When a device is hidden, all evdev event nodes that belong to the same
// physical HID device are grabbed exclusively so no other process receives
// input events from them.  SDL (InputBridge) reads directly from SDL's
// already-opened file descriptors and is therefore unaffected.
//
// This is purely process-level (similar to HidHide's allow-list having only
// InputBridge in it).  Steam uses a separate /dev/uinput virtual device and
// can still function normally.
class LinuxExclusiveMode : public InputExclusiveModeImpl {
public:
    ~LinuxExclusiveMode() override;

    bool HideDevice(SDL_Joystick* joystick) override;
    bool UnhideDevice(SDL_Joystick* joystick) override;
    bool IsAvailable() const override;

private:
    // Mapping from SDL instance_id → list of grabbed (fd, path) pairs.
    struct GrabEntry {
        int fd;
        std::string path;
    };
    std::map<SDL_JoystickID, std::vector<GrabEntry>> m_GrabbedDevices;

    // Resolve ALL input device paths (event* and js*) for a given SDL joystick.
    static std::vector<std::string> FindAllInputDevicePaths(SDL_Joystick* joystick);

    // Release all grabs for one instance.
    void ReleaseInstance(SDL_JoystickID id);
};

#endif // __linux__