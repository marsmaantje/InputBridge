#pragma once
#include <SDL3/SDL.h>
#include <string>

struct DeviceState {
    SDL_JoystickID instance_id = 0;
    std::string name;
    bool is_gamepad = false;
    SDL_Joystick *joystick = nullptr;
    SDL_Gamepad *gamepad = nullptr;
    int num_axes = 0;
    int num_buttons = 0;
    int num_hats = 0;
    
    // Battery information
    SDL_PowerState battery_state = SDL_POWERSTATE_UNKNOWN;
    int battery_percent = -1;  // -1 means unknown, 0-100 otherwise
    // Set to true after the very first battery query so UpdateBatteryInfo()
    // only logs on genuine state/percent changes from that point on.
    // Without this flag the "first read" detection based on value comparison
    // would repeatedly fire for devices that permanently report UNKNOWN/-1
    // (e.g. wired steering wheels without a battery).
    bool battery_initialized = false;

    // Device hide state ---------------------------------------------------
    // When true, the physical device is hidden from all other applications;
    // only InputBridge (and optionally Steam) can still access it.
    bool hide_from_other_apps = false;
};