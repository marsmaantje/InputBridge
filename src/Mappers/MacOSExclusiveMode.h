#pragma once

#ifdef __APPLE__

#include "InputExclusiveModeImpl.h"
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

class MacOSExclusiveMode : public InputExclusiveModeImpl {
public:
    ~MacOSExclusiveMode() override;
    void Apply(SDL_Joystick* joystick, bool enabled) override;

private:
    IOHIDDeviceRef m_MacOSHIDDevice = nullptr;

    void Release();
    void Acquire(SDL_Joystick* joystick);
};

#endif