#pragma once
#include "DeviceState.h"
#include <SDL3/SDL.h>
#include <vector>

// Polls and caches battery information for SDL gamepads / joysticks.
// Extracted from DeviceManager so battery concerns live in one place.
class BatteryMonitor {
  public:
    void SetUpdateInterval(int ms) { m_IntervalMs = static_cast<Uint64>(ms); }
    int  GetUpdateInterval() const { return static_cast<int>(m_IntervalMs); }

    // Periodic refresh. The interval is multiplied by kMinimizedFactor while
    // the application is minimized to reduce background overhead.
    void Update(std::vector<DeviceState>& devices, bool isMinimized);

    // Immediate refresh of a single device.
    void UpdateDevice(DeviceState& dev) const;

  private:
    static constexpr Uint64 kMinimizedFactor = 6;

    Uint64 m_IntervalMs  = 5000;
    Uint64 m_LastUpdateMs = 0;
};
