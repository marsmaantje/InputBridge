#include "WindowsExclusiveMode.h"

#ifdef _WIN32
#include <cstdio>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

WindowsExclusiveMode::~WindowsExclusiveMode() {
    Release();
}

void WindowsExclusiveMode::Apply(SDL_Joystick* joystick, bool enabled) {
    if (enabled) {
        Acquire(joystick);
    } else {
        Release();
    }
}

void WindowsExclusiveMode::Release() {
    if (m_WindowsDIDevice) {
        m_WindowsDIDevice->Unacquire();
        m_WindowsDIDevice->Release();
        m_WindowsDIDevice = nullptr;
    }
    if (m_WindowsDIInterface) {
        m_WindowsDIInterface->Release();
        m_WindowsDIInterface = nullptr;
    }
}

void WindowsExclusiveMode::Acquire(SDL_Joystick* joystick) {
    Release();

    SDL_GUID guid = SDL_GetJoystickGUID(joystick);
    SDL_Log("Attempting to set exclusive mode for device: %s", SDL_GetJoystickName(joystick));
    SDL_Log("Device GUID: %s", SDL_GUIDToString(guid));
    
    HRESULT hr = DirectInput8Create(
        GetModuleHandle(nullptr),
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        (LPVOID*)&m_WindowsDIInterface,
        nullptr
    );
    
    if (FAILED(hr)) {
        SDL_Log("Failed to create DirectInput8 interface: 0x%lx", hr);
        return;
    }
    
    GUID di_guid;
    if (!ConvertSDLGUIDToDirectInputGUID(guid, &di_guid)) {
        SDL_Log("Failed to convert SDL GUID to DirectInput GUID");
        Release();
        return;
    }
    
    hr = m_WindowsDIInterface->CreateDevice(di_guid, &m_WindowsDIDevice, nullptr);
    
    if (FAILED(hr)) {
        SDL_Log("Failed to create DirectInput device: 0x%lx", hr);
        Release();
        return;
    }
    
    hr = m_WindowsDIDevice->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) {
        SDL_Log("Failed to set data format: 0x%lx", hr);
        Release();
        return;
    }
    
    HWND hwnd = GetActiveWindow();
    
    hr = m_WindowsDIDevice->SetCooperativeLevel(
        hwnd,
        DISCL_EXCLUSIVE | DISCL_FOREGROUND
    );
    
    if (FAILED(hr)) {
        SDL_Log("Failed to set cooperative level: 0x%lx", hr);
        Release();
        return;
    }
    
    hr = m_WindowsDIDevice->Acquire();
    if (FAILED(hr)) {
        SDL_Log("Failed to acquire device in exclusive mode: 0x%lx", hr);
        Release();
        return;
    }
    
    SDL_Log("Successfully set exclusive mode for device");
}

bool WindowsExclusiveMode::ConvertSDLGUIDToDirectInputGUID(SDL_GUID sdl_guid, GUID* di_guid) {
    Uint8 data[16];
    memcpy(data, sdl_guid.data, 16);
    
    di_guid->Data1 = *(DWORD*)&data[0];
    di_guid->Data2 = *(WORD*)&data[4];
    di_guid->Data3 = *(WORD*)&data[6];
    memcpy(di_guid->Data4, &data[8], 8);
    
    return true;
}
#endif