#pragma once

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

// Now include SDL and standard library headers
#include <SDL3/SDL.h>
#include <string>
#include <vector>

class DeviceManager;
class PreferencesManager;

class InputMapper {
  public:
    struct AxisConfig {
        int axisIndex = -1;
        bool invert = false;
        float deadzone = 0.05f;
        int outputRange = 0; // 0: -1..1, 1: 0..1, 2: -1..0
    };

    InputMapper(const DeviceManager &deviceManager);
    ~InputMapper();

    void DrawUI();
    std::string UpdateAndBroadcastMessage();

    void LoadConfig(const PreferencesManager &prefs);
    void SaveConfig(PreferencesManager &prefs) const;

  private:
    const DeviceManager &m_DeviceManager;
    SDL_JoystickID m_SelectedDeviceID = 0;

    bool m_ExclusiveMode = false;
    
#ifdef __linux__
    std::vector<std::pair<int, std::string>> m_GrabbedDeviceFds;
#endif

#ifdef _WIN32
    LPDIRECTINPUTDEVICE8 m_WindowsDIDevice = nullptr;
    LPDIRECTINPUT8 m_WindowsDIInterface = nullptr;
    
    bool ConvertSDLGUIDToDirectInputGUID(SDL_GUID sdl_guid, GUID* di_guid);
    void ApplyExclusiveModeWindows(SDL_Joystick *joystick);
#endif

#ifdef __APPLE__
    IOHIDDeviceRef m_MacOSHIDDevice = nullptr;
    void ApplyExclusiveModeMacOS(SDL_Joystick *joystick);
#endif

#ifdef __linux__
    void ApplyExclusiveModeLinux(SDL_Joystick *joystick);
#endif

    AxisConfig m_Steering;
    AxisConfig m_Throttle;
    AxisConfig m_Brake;
    AxisConfig m_Clutch;
    AxisConfig m_Handbrake;

    float ProcessAxis(SDL_Joystick *joystick, const AxisConfig &config);
    void ApplyExclusiveMode();
};