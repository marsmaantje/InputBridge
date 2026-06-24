#pragma once
#include "DeviceVisualizer.h"

class GenericVisualizer : public DeviceVisualizer {
  public:
    void Draw(const DeviceState &dev) override;

  private:
    bool m_showLabels = false;  ///< When true, named labels + Kenney icons replace "Axis N / B N / Hat N"
};
