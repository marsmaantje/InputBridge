#pragma once

#include <memory>
#include <SDL3/SDL.h>

class InputExclusiveModeImpl;

class InputExclusiveMode {
public:
    InputExclusiveMode();
    ~InputExclusiveMode();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void Apply(SDL_Joystick* joystick);

private:
    bool m_Enabled = false;

    std::unique_ptr<InputExclusiveModeImpl> m_Impl;
};