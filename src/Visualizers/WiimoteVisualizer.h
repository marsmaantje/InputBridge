// src/Visualizers/WiimoteVisualizer.h
#pragma once
#include "DeviceVisualizer.h"

class WiimoteVisualizer : public DeviceVisualizer {
  public:
    void Draw(const DeviceState &dev) override;
};
