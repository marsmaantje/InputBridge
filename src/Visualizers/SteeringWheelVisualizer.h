#pragma once
#include "DeviceVisualizer.h"

class SteeringWheelVisualizer : public DeviceVisualizer {
  public:
    void Draw(const DeviceState &dev) override;
};