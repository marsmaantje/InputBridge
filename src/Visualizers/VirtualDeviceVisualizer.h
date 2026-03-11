#pragma once
#include "Devices/DeviceState.h"
#include "Devices/VirtualDeviceManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renders interactive controls (sliders, toggle buttons, hat pad) that let the
// user drive a virtual SDL joystick's axis / button / hat values.
//
// Intended as an extra "Simulate Inputs" tab inside DrawDeviceVisualizer()
// (main.cpp), shown only when SDL_IsJoystickVirtual(dev.instance_id) is true.
// ─────────────────────────────────────────────────────────────────────────────
class VirtualDeviceVisualizer {
public:
    // dev.instance_id must belong to a virtual joystick managed by
    // VirtualDeviceManager.  Modifies the state in-place and calls PushState()
    // at the end so SDL_GetJoystickAxis() reflects the changes immediately.
    void Draw(const DeviceState& dev);
};
