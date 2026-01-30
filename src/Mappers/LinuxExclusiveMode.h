#pragma once

#ifdef __linux__

#include "InputExclusiveModeImpl.h"
#include <vector>
#include <string>
#include <utility>

class LinuxExclusiveMode : public InputExclusiveModeImpl {
public:
    ~LinuxExclusiveMode() override;
    void Apply(SDL_Joystick* joystick, bool enabled) override;

private:
    std::vector<std::pair<int, std::string>> m_GrabbedDeviceFds;
    void ReleaseAll();
    void Grab(SDL_Joystick* joystick);
};

#endif