#pragma once
#include "Devices/DeviceState.h"

class DeviceVisualizer {
public:
    virtual ~DeviceVisualizer() = default;
    virtual void Draw(const DeviceState& dev) = 0;
};