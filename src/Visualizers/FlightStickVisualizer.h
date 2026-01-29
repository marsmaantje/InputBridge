#pragma once
#include "DeviceVisualizer.h"

class FlightStickVisualizer : public DeviceVisualizer {
  public:
    void Draw(const DeviceState &dev) override;
};