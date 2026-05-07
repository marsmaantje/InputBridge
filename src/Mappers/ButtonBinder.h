#pragma once
#include <SDL3/SDL.h>
#include <vector>
#include <optional>
#include <map> // To store baseline states for multiple joysticks

// Forward declaration for DeviceState
struct DeviceState;

struct BoundButtonInfo {
    SDL_JoystickID joystickID;
    int buttonIndex;
};

class ButtonBinder {
public:
    void StartBinding(const std::vector<struct DeviceState>& connectedDevices);
    std::optional<BoundButtonInfo> Update(const std::vector<struct DeviceState>& connectedDevices);
    void Cancel();
    bool IsBindingActive() const;

private:
    std::map<SDL_JoystickID, std::vector<bool>> m_baselineButtonStates;
    bool m_isBinding = false;
};
