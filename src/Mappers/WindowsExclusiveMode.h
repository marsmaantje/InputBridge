#pragma once

#ifdef _WIN32

#include "InputExclusiveModeImpl.h"

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

class WindowsExclusiveMode : public InputExclusiveModeImpl {
public:
    ~WindowsExclusiveMode() override;
    void Apply(SDL_Joystick* joystick, bool enabled) override;

private:
    LPDIRECTINPUTDEVICE8 m_WindowsDIDevice = nullptr;
    LPDIRECTINPUT8 m_WindowsDIInterface = nullptr;

    void Release();
    void Acquire(SDL_Joystick* joystick);
    bool ConvertSDLGUIDToDirectInputGUID(SDL_GUID sdl_guid, GUID* di_guid);
};

#endif