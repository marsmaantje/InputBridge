#pragma once

#include <vector>
#include <string>

// Platform-specific includes MUST come before SDL on Windows to avoid winsock conflicts
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#endif

#ifdef __APPLE__
#include <IOKit/hid/IOHIDDevice.h>
#endif

#include <SDL3/SDL.h>

class InputExclusiveMode {
public:
    InputExclusiveMode();
    ~InputExclusiveMode();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void Apply(SDL_Joystick* joystick);

private:
    bool m_Enabled = false;

#ifdef __linux__
    std::vector<std::pair<int, std::string>> m_GrabbedDeviceFds;
    void ApplyLinux(SDL_Joystick *joystick);
#endif

#ifdef _WIN32
    LPDIRECTINPUTDEVICE8 m_WindowsDIDevice = nullptr;
    LPDIRECTINPUT8 m_WindowsDIInterface = nullptr;
    
    bool ConvertSDLGUIDToDirectInputGUID(SDL_GUID sdl_guid, GUID* di_guid);
    void ApplyWindows(SDL_Joystick *joystick);
#endif

#ifdef __APPLE__
    IOHIDDeviceRef m_MacOSHIDDevice = nullptr;
    void ApplyMacOS(SDL_Joystick *joystick);
#endif
};