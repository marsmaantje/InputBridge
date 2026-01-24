#pragma once
#include <SDL3/SDL.h>
#include <string>

struct DeviceState {
    SDL_JoystickID instance_id;
    std::string name;
    bool is_gamepad;
    SDL_Joystick* joystick;
    SDL_Gamepad* gamepad;
    int num_axes;
    int num_buttons;
    int num_hats;
};