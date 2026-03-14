#pragma once

#include "Devices/DeviceState.h"

class DeviceManager;
class PreferencesManager;

/// Draws the tab bar of visualizers (Raw Inputs, Haptic Test, RPM LEDs, etc.)
/// for a single connected device.  Restores and persists the user's preferred
/// tab via PreferencesManager.
void DrawDeviceVisualizer(const DeviceState&  dev,
                          DeviceManager&      deviceManager,
                          PreferencesManager& prefs);

/// Draws a collapsible header for one connected device.  Includes a battery
/// indicator drawn over the header bar and, when expanded, the full
/// visualizer tab bar.
void DrawDeviceItem(const DeviceState&  dev,
                    DeviceManager&      deviceManager,
                    PreferencesManager& prefs);
