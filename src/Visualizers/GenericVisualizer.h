#pragma once
#include "DeviceVisualizer.h"

class GenericVisualizer : public DeviceVisualizer {
  public:
    /// @param showLabels  When true, named labels + Kenney icons replace "Axis N / B N / Hat N".
    ///                    Controlled from the Settings panel; passed in each frame.
    void Draw(const DeviceState& dev, bool showLabels);

    // DeviceVisualizer interface — routes to Draw(dev, false) for callers that
    // don't supply the flag (should not normally be used for GenericVisualizer).
    void Draw(const DeviceState& dev) override { Draw(dev, false); }
};
