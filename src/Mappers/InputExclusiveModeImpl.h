#pragma once

#include <SDL3/SDL.h>

class InputExclusiveModeImpl {
public:
    virtual ~InputExclusiveModeImpl() = default;
    virtual void Apply(SDL_Joystick* joystick, bool enabled) = 0;
};