#include "InputLabelProvider.h"

#include "Devices/DeviceState.h"
#include "Devices/Wiimote/WiimoteVirtualBridge.h"
#include "SDL3/SDL_gamepad.h"
#include "UI/DeviceIconProvider.h"
#include "UI/KenneyIcons.h"

#include <SDL3/SDL.h>
#include <string>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Identifies which Kenney font family the device uses so per-input codepoints
// can be chosen from the same sheet.  SteamControllerV2 is split out from
// SteamController because the V2 (HEADCRAB) hardware swapped several inputs
// for ones with no equivalent glyph in the Steam Controller font - those are
// resolved from the Steam Deck font instead (see AxisInfoFor/ButtonInfoFor).
enum class FontFamily { Xbox, PlayStation, Switch, SteamDeck, SteamController, SteamControllerV2, Generic, Unknown };

FontFamily GetFontFamily(const DeviceState& dev)
{
    std::string lower = dev.name;
    for (char& c : lower) c = (char)SDL_tolower(c);

    if (lower.find("xbox")        != std::string::npos ||
        lower.find("xinput")      != std::string::npos) return FontFamily::Xbox;

    if (lower.find("dualsense")   != std::string::npos ||
        lower.find("dualshock")   != std::string::npos ||
        lower.find("playstation") != std::string::npos ||
        lower.find("ps3")         != std::string::npos ||
        lower.find("ps4")         != std::string::npos ||
        lower.find("ps5")         != std::string::npos) return FontFamily::PlayStation;

    if (lower.find("switch")      != std::string::npos ||
        lower.find("joy-con")     != std::string::npos ||
        lower.find("joycon")      != std::string::npos) return FontFamily::Switch;

    if (lower.find("steam deck")  != std::string::npos ||
        lower.find("steamdeck")   != std::string::npos) return FontFamily::SteamDeck;

    if (lower.find("steam controller") != std::string::npos ||
        lower.find("steam ctrl")       != std::string::npos)
    {
        // "steam controller v2" → canonical name set by DeviceFactory for V2
        // (HEADCRAB) hardware - same check DeviceIconProvider.cpp uses for
        // the device-header icon.
        if (lower.find("v2") != std::string::npos) return FontFamily::SteamControllerV2;
        return FontFamily::SteamController;
    }

    return FontFamily::Unknown;
}

// ---------------------------------------------------------------------------
// SDL_GamepadAxis → human name + codepoint for each supported font family.
// Returns {name, codepoint} where codepoint==0 means no icon available.
// ---------------------------------------------------------------------------
// `fam` records which font the codepoint belongs to (Unknown == the shared
// generic font).  This lets the caller pick the matching ImFont* per-icon
// instead of per-device, so a family-specific lookup that falls through to
// the generic catch-all below never gets rendered with the wrong font (the
// Kenney fonts share overlapping codepoints, so doing that draws whatever
// unrelated glyph happens to live at that slot in the device's own font).
struct AxisInfo { const char* name; ImWchar cp; FontFamily fam = FontFamily::Unknown; };

AxisInfo AxisInfoFor(SDL_GamepadAxis ga, FontFamily fam)
{
    // Tags a family-specific match with the font family it came from.
    auto withFam = [fam](const char* name, ImWchar cp) -> AxisInfo { return { name, cp, fam }; };

    switch (fam)
    {
    case FontFamily::Xbox:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return withFam("Left Stick X",      0xE051); // xbox_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return withFam("Left Stick Y",      0xE056); // xbox_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return withFam("Right Stick X",     0xE059); // xbox_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return withFam("Right Stick Y",     0xE05E); // xbox_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return withFam("Left Trigger",      0xE047); // xbox_lt
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return withFam("Right Trigger",     0xE04D); // xbox_rt
        default: break;
        }
        break;

    case FontFamily::PlayStation:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return withFam("Left Stick X",      0xE064); // playstation_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return withFam("Left Stick Y",      0xE069); // playstation_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return withFam("Right Stick X",     0xE06C); // playstation_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return withFam("Right Stick Y",     0xE071); // playstation_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return withFam("L2",                0xE07A); // playstation_trigger_l2
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return withFam("R2",                0xE082); // playstation_trigger_r2
        default: break;
        }
        break;

    case FontFamily::Switch:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return withFam("Left Stick X",      0xE05C); // switch_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return withFam("Left Stick Y",      0xE061); // switch_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return withFam("Right Stick X",     0xE064); // switch_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return withFam("Right Stick Y",     0xE069); // switch_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return withFam("ZL",                0xE01C); // switch_button_zl
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return withFam("ZR",                0xE01E); // switch_button_zr
        default: break;
        }
        break;

    case FontFamily::SteamDeck:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return withFam("Left Stick X",      0xE032); // steamdeck_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return withFam("Left Stick Y",      0xE037); // steamdeck_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return withFam("Right Stick X",     0xE03A); // steamdeck_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return withFam("Right Stick Y",     0xE03F); // steamdeck_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return withFam("L2",                0xE009); // steamdeck_button_l2
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return withFam("R2",                0xE015); // steamdeck_button_r2
        default: break;
        }
        break;

    case FontFamily::SteamControllerV2:
        // V2 (HEADCRAB) replaced the trackpad-as-stick compatibility mapping
        // with a real right stick, and both sticks now use the Steam Deck
        // font's stick icons rather than the original Steam Controller font's.
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:  return { "Left Stick X",  0xE032, FontFamily::SteamDeck }; // steamdeck_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:  return { "Left Stick Y",  0xE037, FontFamily::SteamDeck }; // steamdeck_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX: return { "Right Stick X", 0xE03A, FontFamily::SteamDeck }; // steamdeck_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY: return { "Right Stick Y", 0xE03F, FontFamily::SteamDeck }; // steamdeck_stick_r_vertical
        default: break; // triggers etc. fall through to the V1 table below
        }
        [[fallthrough]];

    case FontFamily::SteamController:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return withFam("Left Stick X",      0xE05D); // steam_stick_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return withFam("Left Stick Y",      0xE063); // steam_stick_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return { "Right Pad X", 0xE063, FontFamily::SteamDeck }; // steamdeck_trackpad_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return { "Right Pad Y", 0xE06C, FontFamily::SteamDeck }; // steamdeck_trackpad_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return withFam("Left Trigger",      0xE04D); // steam_lt
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return withFam("Right Trigger",     0xE059); // steam_rt
        default: break;
        }
        break;

    default:
        break;
    }

    // Generic fallback names (no icon for unnamed SDL axes).  Left untagged
    // (fam defaults to Unknown) so InputFont() always resolves these to the
    // shared generic font, regardless of which device this was requested for.
    switch (ga) {
    case SDL_GAMEPAD_AXIS_LEFTX:         return { "Left Stick X",      0xE01D }; // generic_stick_horizontal
    case SDL_GAMEPAD_AXIS_LEFTY:         return { "Left Stick Y",      0xE023 }; // generic_stick_vertical
    case SDL_GAMEPAD_AXIS_RIGHTX:        return { "Right Stick X",     0xE01D }; // generic_stick_horizontal
    case SDL_GAMEPAD_AXIS_RIGHTY:        return { "Right Stick Y",     0xE023 }; // generic_stick_vertical
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return { "Left Trigger",      0 };
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return { "Right Trigger",     0 };
    default:                             return { "Axis",              0 };
    }
}

// ---------------------------------------------------------------------------
// SDL_GamepadButton → human name + codepoint for each supported font family.
// ---------------------------------------------------------------------------
struct ButtonInfo { const char* name; ImWchar cp; FontFamily fam = FontFamily::Unknown; };

ButtonInfo ButtonInfoFor(SDL_GamepadButton gb, FontFamily fam)
{
    // Tags a family-specific match with the font family it came from.
    auto withFam = [fam](const char* name, ImWchar cp) -> ButtonInfo { return { name, cp, fam }; };

    switch (fam)
    {
    case FontFamily::Xbox:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return withFam("A",             0xE004); // xbox_button_a
        case SDL_GAMEPAD_BUTTON_EAST:            return withFam("B",             0xE006); // xbox_button_b
        case SDL_GAMEPAD_BUTTON_WEST:            return withFam("X",             0xE01E); // xbox_button_x
        case SDL_GAMEPAD_BUTTON_NORTH:           return withFam("Y",             0xE020); // xbox_button_y
        case SDL_GAMEPAD_BUTTON_BACK:            return withFam("View",          0xE01C); // xbox_button_view
        case SDL_GAMEPAD_BUTTON_GUIDE:           return withFam("Guide",         0xE041); // xbox_guide
        case SDL_GAMEPAD_BUTTON_START:           return withFam("Menu",          0xE014); // xbox_button_menu
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return withFam("LS",            0xE045); // xbox_ls
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return withFam("RS",            0xE04B); // xbox_rs
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return withFam("LB",            0xE043); // xbox_lb
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return withFam("RB",            0xE049); // xbox_rb
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return withFam("D-Pad Up",      0xE035); // xbox_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return withFam("D-Pad Down",    0xE024); // xbox_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return withFam("D-Pad Left",    0xE028); // xbox_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return withFam("D-Pad Right",   0xE02B); // xbox_dpad_right
        default: break;
        }
        break;

    case FontFamily::PlayStation:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return withFam("Cross",         0xE049); // playstation_button_cross
        case SDL_GAMEPAD_BUTTON_EAST:            return withFam("Circle",        0xE03F); // playstation_button_circle
        case SDL_GAMEPAD_BUTTON_WEST:            return withFam("Square",        0xE04F); // playstation_button_square
        case SDL_GAMEPAD_BUTTON_NORTH:           return withFam("Triangle",      0xE051); // playstation_button_triangle
        case SDL_GAMEPAD_BUTTON_BACK:            return withFam("Share/Create",  0xE01C); // playstation5_button_create (general)
        //case SDL_GAMEPAD_BUTTON_GUIDE:           return withFam("PS",            0xE03D); // playstation5_button_create
        case SDL_GAMEPAD_BUTTON_START:           return withFam("Options",       0xE022); // playstation5_button_options
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return withFam("L3",            0xE04B); // playstation_button_l3
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return withFam("R3",            0xE04D); // playstation_button_r3
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return withFam("L1",            0xE076); // playstation_trigger_l1
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return withFam("R1",            0xE07E); // playstation_trigger_r1
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return withFam("D-Pad Up",      0xE05E); // playstation_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return withFam("D-Pad Down",    0xE055); // playstation_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return withFam("D-Pad Left",    0xE059); // playstation_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return withFam("D-Pad Right",   0xE05C); // playstation_dpad_right
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:        return withFam("Touchpad",      0xE00F); // playstation_button_analog
        case SDL_GAMEPAD_BUTTON_MISC1:           return withFam("Mute",          0xE020); // playstation5_button_mute
        default: break;
        }
        break;

    case FontFamily::Switch:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return withFam("B",             0xE006); // switch_button_b
        case SDL_GAMEPAD_BUTTON_EAST:            return withFam("A",             0xE004); // switch_button_a
        case SDL_GAMEPAD_BUTTON_WEST:            return withFam("Y",             0xE01A); // switch_button_y
        case SDL_GAMEPAD_BUTTON_NORTH:           return withFam("X",             0xE018); // switch_button_x
        case SDL_GAMEPAD_BUTTON_BACK:            return withFam("Minus",         0xE00C); // switch_button_minus
        case SDL_GAMEPAD_BUTTON_GUIDE:           return withFam("Home",          0xE008); // switch_button_home
        case SDL_GAMEPAD_BUTTON_START:           return withFam("Plus",          0xE00E); // switch_button_plus
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return withFam("L Stick Press", 0xE05E); // switch_stick_l_press
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return withFam("R Stick Press", 0xE066); // switch_stick_r_press
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return withFam("L",             0xE00A); // switch_button_l
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return withFam("R",             0xE010); // switch_button_r
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return withFam("D-Pad Up",      0xE03C); // switch_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return withFam("D-Pad Down",    0xE033); // switch_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return withFam("D-Pad Left",    0xE037); // switch_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return withFam("D-Pad Right",   0xE03A); // switch_dpad_right
        default: break;
        }
        break;

    case FontFamily::SteamDeck:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return withFam("A",             0xE001); // steamdeck_button_a
        case SDL_GAMEPAD_BUTTON_EAST:            return withFam("B",             0xE003); // steamdeck_button_b
        case SDL_GAMEPAD_BUTTON_WEST:            return withFam("X",             0xE01D); // steamdeck_button_x
        case SDL_GAMEPAD_BUTTON_NORTH:           return withFam("Y",             0xE01F); // steamdeck_button_y
        case SDL_GAMEPAD_BUTTON_BACK:            return withFam("View",          0xE01B); // steamdeck_button_view
        case SDL_GAMEPAD_BUTTON_GUIDE:           return withFam("Guide",         0xE005); // steamdeck_button_guide
        case SDL_GAMEPAD_BUTTON_START:           return withFam("Options",       0xE00F); // steamdeck_button_options
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return withFam("L Stick Press", 0xE034); // steamdeck_stick_l_press
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return withFam("R Stick Press", 0xE03C); // steamdeck_stick_r_press
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return withFam("L1",            0xE007); // steamdeck_button_l1
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return withFam("R1",            0xE013); // steamdeck_button_r1
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return withFam("D-Pad Up",      0xE02C); // steamdeck_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return withFam("D-Pad Down",    0xE023); // steamdeck_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return withFam("D-Pad Left",    0xE027); // steamdeck_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return withFam("D-Pad Right",   0xE02A); // steamdeck_dpad_right
        default: break;
        }
        break;

    case FontFamily::SteamControllerV2:
        // V2 swaps the trackpad-click compatibility outputs for real stick
        // clicks, and repurposes the TOUCHPAD/MISC2 outputs as a left/right
        // touchpad pair - all rendered with the Steam Deck font's icons
        // (matching the stick-axis change in AxisInfoFor).
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:   return { "L Stick",     0xE034, FontFamily::SteamDeck }; // steamdeck_stick_l_press
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:  return { "R Stick",     0xE03C, FontFamily::SteamDeck }; // steamdeck_stick_r_press
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:     return { "L Touchpad",  0xE04B, FontFamily::SteamDeck }; // steamdeck_trackpad_l
        case SDL_GAMEPAD_BUTTON_MISC2:        return { "R Touchpad",  0xE05E, FontFamily::SteamDeck }; // steamdeck_trackpad_r
        case SDL_GAMEPAD_BUTTON_BACK:         return { "Back",        0xE01B, FontFamily::SteamDeck }; // steamdeck_button_view
        case SDL_GAMEPAD_BUTTON_START:        return { "Start",       0xE00F, FontFamily::SteamDeck }; // steamdeck_button_options
        case SDL_GAMEPAD_BUTTON_DPAD_UP:      return { "D-Pad Up",    0xE02C, FontFamily::SteamDeck }; // steamdeck_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:    return { "D-Pad Down",  0xE023, FontFamily::SteamDeck }; // steamdeck_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:    return { "D-Pad Left",  0xE027, FontFamily::SteamDeck }; // steamdeck_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:   return { "D-Pad Right", 0xE02A, FontFamily::SteamDeck }; // steamdeck_dpad_right
        default: break; // A/B/X/Y, paddles, quick-access, etc. fall through to the V1 table
        }
        [[fallthrough]];

    case FontFamily::SteamController:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return withFam("A",             0xE022); // steam_button_a
        case SDL_GAMEPAD_BUTTON_EAST:            return withFam("B",             0xE024); // steam_button_b
        case SDL_GAMEPAD_BUTTON_WEST:            return withFam("X",             0xE036); // steam_button_x
        case SDL_GAMEPAD_BUTTON_NORTH:           return withFam("Y",             0xE038); // steam_button_y
        case SDL_GAMEPAD_BUTTON_BACK:            return withFam("Back",          0xE026); // steam_button_back_icon
        case SDL_GAMEPAD_BUTTON_GUIDE:           return withFam("Steam",         0xE016); // controller_icon
        case SDL_GAMEPAD_BUTTON_START:           return withFam("Start",         0xE034); // steam_button_start_icon
        // Left stick click is a real analog stick press; the right side is the
        // trackpad's click, which has its own dedicated glyph in this font.
        // (V1 only - V2 overrides both above.)
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return withFam("L Stick",       0xE05E); // steam_stick_l_press
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return withFam("Pad Click",     0xE050); // steam_pad_center
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return withFam("LB",            0xE049); // steam_lb
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return withFam("RB",            0xE055); // steam_rb
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return withFam("D-Pad Up",      0xE045); // steam_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return withFam("D-Pad Down",    0xE03C); // steam_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return withFam("D-Pad Left",    0xE040); // steam_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return withFam("D-Pad Right",   0xE043); // steam_dpad_right
        // V2 adds back-grip paddles, a touchpad-click, and a quick-access button.
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:    return withFam("L4",           0xE004); // controller_button_l4
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:    return withFam("L5",           0xE006); // controller_button_l5
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:   return withFam("R4",           0xE010); // controller_button_r4
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:   return withFam("R5",           0xE012); // controller_button_r5
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:        return { "Touchpad",     0xE045, FontFamily::SteamDeck }; // steamdeck_trackpad_all (V1 only)
        case SDL_GAMEPAD_BUTTON_MISC1:           return withFam("Quick Access", 0xE00A); // controller_button_quickaccess
        default: break;
        }
        break;

    default:
        break;
    }

    // Generic fallback for unknown family.  Left untagged (fam defaults to
    // Unknown) so InputFont() always resolves these to the shared generic
    // font, regardless of which device this was requested for.  These newer
    // SDL_GamepadButton values (paddles, touchpad, misc) have no glyph in the
    // generic font, but still get a real name instead of falling through to
    // the catch-all "Button" below.
    switch (gb) {
    case SDL_GAMEPAD_BUTTON_SOUTH:           return { "South",        0xE000 }; // generic_button
    case SDL_GAMEPAD_BUTTON_EAST:            return { "East",         0xE000 };
    case SDL_GAMEPAD_BUTTON_WEST:            return { "West",         0xE000 };
    case SDL_GAMEPAD_BUTTON_NORTH:           return { "North",        0xE000 };
    case SDL_GAMEPAD_BUTTON_BACK:            return { "Back",         0 };
    case SDL_GAMEPAD_BUTTON_GUIDE:           return { "Guide",        0 };
    case SDL_GAMEPAD_BUTTON_START:           return { "Start",        0 };
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return { "L Stick",      0xE01F }; // generic_stick_press
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return { "R Stick",      0xE01F };
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return { "L Shoulder",   0 };
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return { "R Shoulder",   0 };
    case SDL_GAMEPAD_BUTTON_DPAD_UP:         return { "D-Pad Up",     0 };
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return { "D-Pad Down",   0 };
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return { "D-Pad Left",   0 };
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return { "D-Pad Right",  0 };
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:    return { "L Paddle 1",   0 };
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:    return { "L Paddle 2",   0 };
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:   return { "R Paddle 1",   0 };
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:   return { "R Paddle 2",   0 };
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:        return { "Touchpad",     0 };
    case SDL_GAMEPAD_BUTTON_MISC1:           return { "Misc 1",       0 };
    case SDL_GAMEPAD_BUTTON_MISC2:           return { "Misc 2",       0 };
    case SDL_GAMEPAD_BUTTON_MISC3:           return { "Misc 3",       0 };
    case SDL_GAMEPAD_BUTTON_MISC4:           return { "Misc 4",       0 };
    case SDL_GAMEPAD_BUTTON_MISC5:           return { "Misc 5",       0 };
    case SDL_GAMEPAD_BUTTON_MISC6:           return { "Misc 6",       0 };
    default:                                 return { "Button",       0 };
    }
}

// Builds a DeviceIcon from a font pointer + codepoint.
// Returns an invalid DeviceIcon when font is null or cp is 0.
DeviceIcon MakeIcon(ImFont* font, ImWchar cp)
{
    if (!font || cp == 0) return {};
    DeviceIcon icon;
    icon.font      = font;
    icon.codepoint = cp;
    // KENNEY_ICON_STR is a macro that may not accept runtime cp values here,
    // so build the UTF-8 bytes locally into a thread-local buffer and
    // point glyph at it.
    static thread_local char _kenney_buf[4];
    auto _arr = KenneyIconUTF8(cp);
    std::memcpy(_kenney_buf, _arr.data(), 4);
    icon.glyph = _kenney_buf;
    return icon;
}

// Maps a font family directly to its ImFont* in the KenneyFonts registry.
// This is the authoritative lookup for an icon's font: AxisInfoFor() and
// ButtonInfoFor() tag every returned codepoint with the family it actually
// belongs to, and that tag is not always the same as the device's own
// detected family - e.g. the Steam Controller V2's back-grip paddles are
// rendered using the Steam Deck font (steam_controller.ttf has no glyphs
// for them), even though the device itself is FontFamily::SteamController.
ImFont* FontForFamily(FontFamily fam)
{
    KenneyFonts& fonts = KenneyFonts::Get();
    switch (fam)
    {
    case FontFamily::Xbox:            return fonts.xbox;
    case FontFamily::PlayStation:     return fonts.playstation;
    case FontFamily::Switch:          return fonts.nintendoSwitch;
    case FontFamily::SteamDeck:       return fonts.steamDeck;
    case FontFamily::SteamController:
    case FontFamily::SteamControllerV2: return fonts.steamController;
    case FontFamily::Generic:
    case FontFamily::Unknown:
    default:                          return fonts.generic;
    }
}

// Chooses the correct icon font for an input icon.  `fam` comes from
// AxisInfo::fam / ButtonInfo::fam - the family the *codepoint* belongs to -
// not necessarily the device's own family, so it is always resolved
// directly rather than assumed to equal the device's font.
ImFont* InputFont(const DeviceState& dev, FontFamily fam)
{
    (void)dev; // no longer needed now that every codepoint is tagged with its real family
    ImFont* f = FontForFamily(fam);
    return f ? f : KenneyFonts::Get().generic;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Wiimote virtual-bridge naming
// ---------------------------------------------------------------------------
// WiimoteVirtualBridge (Devices/Wiimote/WiimoteVirtualBridge.h) creates a
// virtual joystick per connected Wiimote/Balance Board whose axis/button
// indices hold Wiimote-specific data (accelerometer, IR, Nunchuk, Classic
// Controller, Motion Plus, or Balance Board corner weights) rather than a
// standard gamepad's sticks/triggers/face buttons. It's intentionally NOT
// typed as SDL_JOYSTICK_TYPE_GAMEPAD (see its Attach()), so dev.gamepad is
// null here and this never collides with the SDL_GamepadBinding path below
// - but the exact-name match is checked first regardless, so this stays
// correct even if that ever changes.
namespace {
bool IsWiimoteBridgeDevice(const DeviceState& dev, bool* isBalance)
{
    if (dev.name == InputBridge::Wiimote::kWiimoteBridgeDeviceName) { *isBalance = false; return true; }
    if (dev.name == InputBridge::Wiimote::kBalanceBoardBridgeDeviceName) { *isBalance = true; return true; }
    return false;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// InputLabelProvider - public API
// ---------------------------------------------------------------------------

InputLabel InputLabelProvider::GetAxisLabel(const DeviceState& dev, int axis)
{
    InputLabel result;

    bool isBalance = false;
    if (IsWiimoteBridgeDevice(dev, &isBalance))
    {
        const char* n = isBalance
            ? InputBridge::Wiimote::BalanceBoardBridgeAxisName(axis)
            : InputBridge::Wiimote::WiimoteBridgeAxisName(axis);
        result.name = n ? n : ("Axis " + std::to_string(axis));
        // No Kenney glyphs exist for Wiimote-specific inputs (accelerometer,
        // IR position, Motion Plus gyro axes) - the generic stick icon is a
        // reasonable stand-in for anything analog, same choice the
        // non-gamepad fallback below makes for real unnamed joysticks.
        const ImWchar cp = (axis % 2 == 0) ? 0xE01D : 0xE023; // generic_stick_horizontal / _vertical
        result.icon = MakeIcon(KenneyFonts::Get().generic, cp);
        return result;
    }

    if (dev.gamepad)
    {
        // Walk every SDL_GamepadAxis and check whether its binding maps to
        // this joystick axis.
        const FontFamily fam  = GetFontFamily(dev);

        int count = 0;
        SDL_GamepadBinding** bindings = SDL_GetGamepadBindings(dev.gamepad, &count);
        if (bindings)
        {
            for (int i = 0; i < count; ++i)
            {
                SDL_GamepadBinding* bind = bindings[i];
                if (bind->input_type == SDL_GAMEPAD_BINDTYPE_AXIS && bind->input.axis.axis == axis)
                {
                    if (bind->output_type == SDL_GAMEPAD_BINDTYPE_AXIS)
                    {
                        AxisInfo info = AxisInfoFor(bind->output.axis.axis, fam);
                        result.name = info.name;
                        // info.fam reflects which font this specific codepoint
                        // belongs to (it may differ from the device's overall
                        // family when no device-specific icon exists for this
                        // particular axis) - always resolve the font from it,
                        // never from the device's family directly.
                        result.icon = MakeIcon(InputFont(dev, info.fam), info.cp);
                        SDL_free(bindings);
                        return result;
                    }
                }
            }
            SDL_free(bindings);
        }

        // Axis is bound but not one of the standard SDL_GamepadAxis values
        // (e.g. an extra paddle axis).  Use a numbered fallback with a
        // generic stick icon.
        result.name = "Axis " + std::to_string(axis);
        result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE01B); // generic_stick
        return result;
    }

    // Non-gamepad path: no rich binding data available.
    result.name = "Axis " + std::to_string(axis);
    // generic_stick_horizontal for even indices (likely X), vertical for odd (likely Y)
    const ImWchar cp = (axis % 2 == 0) ? 0xE01D : 0xE023; // generic_stick_horizontal / _vertical
    result.icon = MakeIcon(KenneyFonts::Get().generic, cp);
    return result;
}

InputLabel InputLabelProvider::GetButtonLabel(const DeviceState& dev, int button)
{
    InputLabel result;

    bool isBalance = false;
    if (IsWiimoteBridgeDevice(dev, &isBalance))
    {
        const char* n = isBalance
            ? InputBridge::Wiimote::BalanceBoardBridgeButtonName(button)
            : InputBridge::Wiimote::WiimoteBridgeButtonName(button);
        result.name = n ? n : ("Button " + std::to_string(button));
        result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE000); // generic_button
        return result;
    }

    if (dev.gamepad)
    {
        const FontFamily fam  = GetFontFamily(dev);

        int count = 0;
        SDL_GamepadBinding** bindings = SDL_GetGamepadBindings(dev.gamepad, &count);
        if (bindings)
        {
            for (int i = 0; i < count; ++i)
            {
                SDL_GamepadBinding* bind = bindings[i];
                if (bind->input_type == SDL_GAMEPAD_BINDTYPE_BUTTON && bind->input.button == button)
                {
                    if (bind->output_type == SDL_GAMEPAD_BINDTYPE_BUTTON)
                    {
                        ButtonInfo info = ButtonInfoFor(bind->output.button, fam);
                        result.name = info.name;
                        // See GetAxisLabel: resolve the font from info.fam, not
                        // the device's family, so a fallthrough to the generic
                        // table never gets drawn with the wrong font.
                        result.icon = MakeIcon(InputFont(dev, info.fam), info.cp);
                        SDL_free(bindings);
                        return result;
                    }
                }
            }
            SDL_free(bindings);
        }

        // Extra / unmapped button - numbered fallback.
        result.name = "Button " + std::to_string(button);
        result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE000); // generic_button
        return result;
    }

    // Non-gamepad path.
    result.name = "Button " + std::to_string(button);
    result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE000); // generic_button
    return result;
}

InputLabel InputLabelProvider::GetHatLabel(const DeviceState& dev, int hat, uint8_t hatValue)
{
    InputLabel result;
    result.name = "Hat " + std::to_string(hat);

    // WiimoteVirtualBridge attaches a real Wiimote's bridge joystick with
    // nhats=1 for its main D-Pad (Balance Boards have no D-Pad and stay at
    // nhats=0 - see Attach()), so this can be reached for that device;
    // override the numbered fallback name with the real label the same way
    // GetAxisLabel/GetButtonLabel do. Icon selection below already produces
    // a reasonable generic D-Pad glyph for this device family, so only the
    // name needs the Wiimote-aware override.
    bool isBalance = false;
    if (IsWiimoteBridgeDevice(dev, &isBalance) && !isBalance)
    {
        const char* n = InputBridge::Wiimote::WiimoteBridgeHatName(hat);
        if (n) result.name = n;
    }

    const FontFamily fam = GetFontFamily(dev);

    // Hats have no SDL gamepad binding equivalent (SDL maps them to D-Pad
    // buttons internally) - but we can still reuse the per-family D-Pad
    // icon set from ButtonInfoFor() by translating the held direction(s)
    // into the matching SDL_GAMEPAD_BUTTON_DPAD_* value.  Diagonals prefer
    // the vertical component so e.g. UP+RIGHT shows the "Up" glyph.
    SDL_GamepadButton dpad;
    if      (hatValue & SDL_HAT_UP)    dpad = SDL_GAMEPAD_BUTTON_DPAD_UP;
    else if (hatValue & SDL_HAT_DOWN)  dpad = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    else if (hatValue & SDL_HAT_LEFT)  dpad = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    else if (hatValue & SDL_HAT_RIGHT) dpad = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    else
    {
        // Centered - no direction held. Steam Controller V2 has a dedicated
        // neutral D-Pad glyph in the Steam Deck font; everyone else falls
        // back to the generic joystick glyph.
        if (fam == FontFamily::SteamController)
            result.icon = MakeIcon(InputFont(dev, FontFamily::SteamController), 0xE03A); // steam_dpad
        else if (fam == FontFamily::SteamControllerV2)
            result.icon = MakeIcon(InputFont(dev, FontFamily::SteamDeck), 0xE021); // steamdeck_dpad
        else
            result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE013); // generic_joystick
        return result;
    }

    ButtonInfo info = ButtonInfoFor(dpad, fam);
    result.icon = MakeIcon(InputFont(dev, info.fam), info.cp);
    return result;
}