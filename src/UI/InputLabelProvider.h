#pragma once
// InputLabelProvider.h
//
// Resolves a raw joystick axis / button / hat index into a human-readable
// name and, when possible, a matching Kenney icon glyph.
//
// For gamepad devices (DeviceState::is_gamepad == true / dev.gamepad != null)
// SDL's binding tables are queried so that e.g. joystick axis 0 becomes
// "Left Stick X" with the correct device-specific stick icon.
//
// For non-gamepad joysticks (flight sticks, wheels, etc.) the SDL joystick
// name API is used where available, then numbered fallbacks are used.
//
// Usage (inside an ImGui frame):
//
//   auto label = InputLabelProvider::GetAxisLabel(dev, i);
//   // Render optional Kenney icon
//   if (label.icon.IsValid()) {
//       ImGui::PushFont(label.icon.font);
//       ImGui::Text("%s", label.icon.glyph);
//       ImGui::PopFont();
//       ImGui::SameLine();
//   }
//   ImGui::Text("%s", label.name.c_str());

#include "UI/DeviceIconProvider.h"  // DeviceIcon
#include <string>

struct DeviceState;

// ---------------------------------------------------------------------------
// InputLabel
// ---------------------------------------------------------------------------
struct InputLabel
{
    std::string name;       ///< Human-readable input name, e.g. "Left Stick X"
    DeviceIcon  icon;       ///< Optional Kenney icon (may be IsValid()==false)
};

// ---------------------------------------------------------------------------
// InputLabelProvider
// ---------------------------------------------------------------------------
class InputLabelProvider
{
public:
    /// Label for joystick axis index `axis`.
    static InputLabel GetAxisLabel  (const DeviceState& dev, int axis);

    /// Label for joystick button index `button`.
    static InputLabel GetButtonLabel(const DeviceState& dev, int button);

    /// Label for joystick hat index `hat`.
    static InputLabel GetHatLabel   (const DeviceState& dev, int hat);
};
