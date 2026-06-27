#pragma once

#ifdef __APPLE__

#include "InputExclusiveModeImpl.h"
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <map>

// macOS backend: IOHIDOptionsTypeSeizeDevice, per device instance.
//
// Opening a HID device with kIOHIDOptionsTypeSeizeDevice prevents all other
// processes from accessing it.  SDL continues to work because InputBridge's
// own process already owns the device handle.
class MacOSExclusiveMode : public InputExclusiveModeImpl {
public:
    ~MacOSExclusiveMode() override;

    bool HideDevice(SDL_Joystick* joystick) override;
    bool UnhideDevice(SDL_Joystick* joystick) override;
    bool IsAvailable() const override;

private:
    // Mapping from SDL instance_id → seized IOHIDDeviceRef.
    std::map<SDL_JoystickID, IOHIDDeviceRef> m_SeizedDevices;

    // Find the IOHIDDeviceRef for a given vendor/product.
    // Returns nullptr if not found.
    static IOHIDDeviceRef FindHIDDevice(Uint16 vendorId, Uint16 productId);
};

#endif // __APPLE__