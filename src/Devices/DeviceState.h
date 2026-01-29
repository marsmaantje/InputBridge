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
};