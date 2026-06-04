#include "App/Log.h"
#include "MacOSExclusiveMode.h"

#ifdef __APPLE__

#include <SDL3/SDL.h>

// kIOMainPortDefault was renamed from kIOMasterPortDefault in newer SDKs.
#ifndef kIOMainPortDefault
#define kIOMainPortDefault kIOMasterPortDefault
#endif

// ─── IsAvailable ──────────────────────────────────────────────────────────────

bool MacOSExclusiveMode::IsAvailable() const {
    // IOHIDManager is always present on macOS 10.5+.
    return true;
}

// ─── FindHIDDevice ────────────────────────────────────────────────────────────

IOHIDDeviceRef MacOSExclusiveMode::FindHIDDevice(Uint16 vendorId, Uint16 productId) {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOHIDDeviceKey);
    if (!matching) return nullptr;

    int vid = vendorId, pid = productId;
    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    CFDictionarySetValue(matching, CFSTR(kIOHIDVendorIDKey),  vidRef);
    CFDictionarySetValue(matching, CFSTR(kIOHIDProductIDKey), pidRef);
    CFRelease(vidRef);
    CFRelease(pidRef);

    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS)
        return nullptr;

    IOHIDDeviceRef found = nullptr;
    io_object_t obj;
    while ((obj = IOIteratorNext(iter)) && !found) {
        IOHIDDeviceRef dev = IOHIDDeviceCreate(kCFAllocatorDefault, obj);
        if (dev) found = dev; // caller takes ownership
        IOObjectRelease(obj);
    }
    IOObjectRelease(iter);
    return found;
}

// ─── HideDevice ───────────────────────────────────────────────────────────────

bool MacOSExclusiveMode::HideDevice(SDL_Joystick* joystick) {
    SDL_JoystickID id = SDL_GetJoystickID(joystick);

    // Already seized?
    if (m_SeizedDevices.count(id)) return true;

    Uint16 vid = SDL_GetJoystickVendor(joystick);
    Uint16 pid = SDL_GetJoystickProduct(joystick);

    IOHIDDeviceRef dev = FindHIDDevice(vid, pid);
    if (!dev) {
        LOG_INFO("ExclusiveMode", "MacOSExclusiveMode: could not find IOHIDDevice for '%s' "
                "(VID=%04X PID=%04X).", SDL_GetJoystickName(joystick), vid, pid);
        return false;
    }

    IOReturn ret = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeSeizeDevice);
    if (ret != kIOReturnSuccess) {
        LOG_ERROR("ExclusiveMode", "MacOSExclusiveMode: IOHIDDeviceOpen(seize) failed for '%s': 0x%x.",
                SDL_GetJoystickName(joystick), ret);
        CFRelease(dev);
        return false;
    }

    m_SeizedDevices[id] = dev;
    LOG_INFO("ExclusiveMode", "MacOSExclusiveMode: '%s' is now hidden.", SDL_GetJoystickName(joystick));
    return true;
}

// ─── UnhideDevice ─────────────────────────────────────────────────────────────

bool MacOSExclusiveMode::UnhideDevice(SDL_Joystick* joystick) {
    SDL_JoystickID id = SDL_GetJoystickID(joystick);
    auto it = m_SeizedDevices.find(id);
    if (it == m_SeizedDevices.end()) return true; // was not hidden

    IOHIDDeviceClose(it->second, kIOHIDOptionsTypeSeizeDevice);
    CFRelease(it->second);
    m_SeizedDevices.erase(it);
    LOG_INFO("ExclusiveMode", "MacOSExclusiveMode: '%s' is now visible.", SDL_GetJoystickName(joystick));
    return true;
}

// ─── Destructor ───────────────────────────────────────────────────────────────

MacOSExclusiveMode::~MacOSExclusiveMode() {
    for (auto& [id, dev] : m_SeizedDevices) {
        IOHIDDeviceClose(dev, kIOHIDOptionsTypeSeizeDevice);
        CFRelease(dev);
    }
    m_SeizedDevices.clear();
}

#endif // __APPLE__