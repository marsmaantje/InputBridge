#include "MacOSExclusiveMode.h"

#ifdef __APPLE__

#ifndef kIOMainPortDefault
#define kIOMainPortDefault kIOMasterPortDefault
#endif

MacOSExclusiveMode::~MacOSExclusiveMode() {
    Release();
}

void MacOSExclusiveMode::Apply(SDL_Joystick* joystick, bool enabled) {
    if (enabled) {
        Acquire(joystick);
    } else {
        Release();
    }
}

void MacOSExclusiveMode::Release() {
    if (m_MacOSHIDDevice) {
        SDL_Log("Releasing exclusive mode on macOS");
        IOHIDDeviceClose(m_MacOSHIDDevice, kIOHIDOptionsTypeSeizeDevice);
        CFRelease(m_MacOSHIDDevice);
        m_MacOSHIDDevice = nullptr;
    }
}

void MacOSExclusiveMode::Acquire(SDL_Joystick* joystick) {
    Release();
    
    SDL_Log("Attempting to set exclusive mode on macOS for device: %s", SDL_GetJoystickName(joystick));
    
    Uint16 vendor = SDL_GetJoystickVendor(joystick);
    Uint16 product = SDL_GetJoystickProduct(joystick);
    
    CFMutableDictionaryRef matching_dict = IOServiceMatching(kIOHIDDeviceKey);
    if (!matching_dict) return;
    
    CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &vendor);
    CFNumberRef product_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &product);
    
    CFDictionarySetValue(matching_dict, CFSTR(kIOHIDVendorIDKey), vendor_ref);
    CFDictionarySetValue(matching_dict, CFSTR(kIOHIDProductIDKey), product_ref);
    
    CFRelease(vendor_ref);
    CFRelease(product_ref);
    
    io_iterator_t iterator = 0;
    kern_return_t result = IOServiceGetMatchingServices(kIOMainPortDefault, matching_dict, &iterator);
    
    if (result != KERN_SUCCESS) return;
    
    io_object_t device_obj;
    while ((device_obj = IOIteratorNext(iterator))) {
        IOHIDDeviceRef hid_device = IOHIDDeviceCreate(kCFAllocatorDefault, device_obj);
        if (hid_device) {
            IOReturn ret = IOHIDDeviceOpen(hid_device, kIOHIDOptionsTypeSeizeDevice);
            if (ret == kIOReturnSuccess) {
                SDL_Log("Successfully opened device in exclusive mode");
                m_MacOSHIDDevice = hid_device;
                IOObjectRelease(device_obj);
                break;
            } else {
                CFRelease(hid_device);
            }
        }
        IOObjectRelease(device_obj);
    }
    IOObjectRelease(iterator);
}
#endif