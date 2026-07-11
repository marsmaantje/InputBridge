#pragma once

#ifdef __linux__

#include "InputExclusiveModeImpl.h"
#include <string>
#include <vector>
#include <map>

// Linux backend: EVIOCGRAB exclusive evdev grab, per device instance.
//
// When a device is hidden, all evdev event nodes that belong to the same
// physical HID device are grabbed exclusively so no other process receives
// button/axis events from them.  SDL (InputBridge) reads directly from SDL's
// already-opened file descriptors and is therefore unaffected.
//
// LIMITATION: this does NOT block Steam Input from hijacking gyro/accel data
// on sensor-capable pads (DualSense, DualShock4, Switch Pro, etc). Steam's
// controller backend talks to /dev/hidrawN directly for those, not evdev, and
// Linux's hidraw has no exclusive-access primitive equivalent to EVIOCGRAB -
// any process can open a hidraw node concurrently, and since Steam and
// InputBridge normally run as the same Linux user there's no filesystem
// permission boundary to exploit either. There is currently no Linux
// equivalent of Windows' HidHide filter driver for this. The practical
// workaround is the same as on Linux without this feature at all: disable
// Steam Input for the affected controller type in Steam's settings.
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