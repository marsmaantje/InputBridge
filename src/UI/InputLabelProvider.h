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
//       ImGui::Text("%s", label.icon.glyph());
//       ImGui::PopFont();
//       ImGui::SameLine();
//   }
//   ImGui::Text("%s", label.name.c_str());

#include "UI/DeviceIconProvider.h"  // DeviceIcon
#include <cstdint>
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
// DpadDirectionIcons
// ---------------------------------------------------------------------------
// Four per-direction icons for a single hat, each already resolved to
// whichever glyph matches that direction's current held/idle state. Only the
// Wii font ships distinct "held" (filled) and "idle" (outline) glyphs for
// each D-Pad direction - every other family only has one glyph per direction
// and relies on the caller's own pressed/unpressed tint instead (see
// GenericVisualizer's Button rendering) - so for any non-Wii device all four
// icons come back !IsValid() and callers should just fall back to
// GetHatLabel()'s single icon.
struct DpadDirectionIcons
{
    DeviceIcon up, down, left, right;
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

    /// Label for joystick hat index `hat`.  `hatValue` is the current
    /// SDL_GetJoystickHat() bitmask (SDL_HAT_UP / DOWN / LEFT / RIGHT) - when
    /// a direction is held, the icon is the matching device-specific D-Pad
    /// glyph (reusing the same per-family set as GetButtonLabel) instead of a
    /// static generic joystick icon.
    static InputLabel GetHatLabel   (const DeviceState& dev, int hat, uint8_t hatValue);

    /// Four-way breakdown of `hatValue` for devices whose font has separate
    /// held/idle D-Pad glyphs per direction (currently just Wii) - lets a
    /// caller draw a little up/down/left/right cluster that lights up the
    /// held direction(s) instead of GetHatLabel()'s single combined icon.
    /// See DpadDirectionIcons's comment for the all-other-families fallback.
    static DpadDirectionIcons GetHatDirectionIcons(const DeviceState& dev, uint8_t hatValue);
};
