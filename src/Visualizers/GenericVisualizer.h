#pragma once
#include "DeviceVisualizer.h"

class GenericVisualizer : public DeviceVisualizer {
  public:
    void Draw(const DeviceState &dev) override;
};