#pragma once
#include "DeviceVisualizer.h"

class GamepadVisualizer : public DeviceVisualizer {
public:
    void Draw(const DeviceState& dev) override;
};