#pragma once

#include "Devices/DeviceState.h"

class DeviceManager;
class PreferencesManager;

/// Draws the tab bar of visualizers (Raw Inputs, Haptic Test, etc.)
/// for a single connected device.  Restores and persists the user's preferred
/// tab via PreferencesManager.
void DrawDeviceVisualizer(DeviceState&         dev,
                          DeviceManager&      deviceManager,
                          PreferencesManager& prefs,
                          bool                show_named_inputs);

/// Draws a collapsible header for one connected device.  Includes a battery
/// indicator, a "Device Visibility" section for the per-device hide toggle,
/// and the full visualizer tab bar when expanded.
///
/// DeviceState is taken by non-const reference because the hide toggle
/// updates dev.hide_from_other_apps in place.
void DrawDeviceItem(DeviceState&        dev,
                    DeviceManager&      deviceManager,
                    PreferencesManager& prefs,
                    bool                show_named_inputs);