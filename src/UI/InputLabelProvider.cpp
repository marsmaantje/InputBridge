#include "InputLabelProvider.h"

#include "Devices/DeviceState.h"
#include "UI/DeviceIconProvider.h"
#include "UI/KenneyIcons.h"

#include <SDL3/SDL.h>
#include <string>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Returns the KenneyFonts singleton font pointer that matches the device font
// selected by DeviceIconProvider, so input icons always use the same family
// as the device header icon.
ImFont* DeviceFont(const DeviceState& dev)
{
    return DeviceIconProvider::GetIcon(dev).font;
}

// Identifies which Kenney font family the device uses so per-input codepoints
// can be chosen from the same sheet.
enum class FontFamily { Xbox, PlayStation, Switch, SteamDeck, SteamController, Generic, Unknown };

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
        lower.find("steam ctrl")       != std::string::npos) return FontFamily::SteamController;

    return FontFamily::Unknown;
}

// ---------------------------------------------------------------------------
// SDL_GamepadAxis → human name + codepoint for each supported font family.
// Returns {name, codepoint} where codepoint==0 means no icon available.
// ---------------------------------------------------------------------------
struct AxisInfo { const char* name; ImWchar cp; };

AxisInfo AxisInfoFor(SDL_GamepadAxis ga, FontFamily fam)
{
    switch (fam)
    {
    case FontFamily::Xbox:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return { "Left Stick X",      0xE051 }; // xbox_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return { "Left Stick Y",      0xE056 }; // xbox_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return { "Right Stick X",     0xE059 }; // xbox_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return { "Right Stick Y",     0xE05E }; // xbox_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return { "Left Trigger",      0xE047 }; // xbox_lt
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return { "Right Trigger",     0xE04D }; // xbox_rt
        default: break;
        }
        break;

    case FontFamily::PlayStation:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return { "Left Stick X",      0xE064 }; // playstation_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return { "Left Stick Y",      0xE069 }; // playstation_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return { "Right Stick X",     0xE06C }; // playstation_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return { "Right Stick Y",     0xE071 }; // playstation_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return { "L2",                0xE07A }; // playstation_trigger_l2
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return { "R2",                0xE082 }; // playstation_trigger_r2
        default: break;
        }
        break;

    case FontFamily::Switch:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return { "Left Stick X",      0xE05C }; // switch_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return { "Left Stick Y",      0xE061 }; // switch_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return { "Right Stick X",     0xE064 }; // switch_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return { "Right Stick Y",     0xE069 }; // switch_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return { "ZL",                0xE01C }; // switch_button_zl
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return { "ZR",                0xE01E }; // switch_button_zr
        default: break;
        }
        break;

    case FontFamily::SteamDeck:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return { "Left Stick X",      0xE032 }; // steamdeck_stick_l_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return { "Left Stick Y",      0xE037 }; // steamdeck_stick_l_vertical
        case SDL_GAMEPAD_AXIS_RIGHTX:        return { "Right Stick X",     0xE03A }; // steamdeck_stick_r_horizontal
        case SDL_GAMEPAD_AXIS_RIGHTY:        return { "Right Stick Y",     0xE03F }; // steamdeck_stick_r_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return { "L2",                0xE009 }; // steamdeck_button_l2
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return { "R2",                0xE015 }; // steamdeck_button_r2
        default: break;
        }
        break;

    case FontFamily::SteamController:
        switch (ga) {
        case SDL_GAMEPAD_AXIS_LEFTX:         return { "Left Stick X",      0xE05D }; // steam_stick_horizontal
        case SDL_GAMEPAD_AXIS_LEFTY:         return { "Left Stick Y",      0xE063 }; // steam_stick_vertical
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return { "Left Trigger",      0xE04D }; // steam_lt
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return { "Right Trigger",     0xE059 }; // steam_rt
        default: break;
        }
        break;

    default:
        break;
    }

    // Generic fallback names (no icon for unnamed SDL axes)
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
struct ButtonInfo { const char* name; ImWchar cp; };

ButtonInfo ButtonInfoFor(SDL_GamepadButton gb, FontFamily fam)
{
    switch (fam)
    {
    case FontFamily::Xbox:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return { "A",             0xE004 }; // xbox_button_a
        case SDL_GAMEPAD_BUTTON_EAST:            return { "B",             0xE006 }; // xbox_button_b
        case SDL_GAMEPAD_BUTTON_WEST:            return { "X",             0xE01E }; // xbox_button_x
        case SDL_GAMEPAD_BUTTON_NORTH:           return { "Y",             0xE020 }; // xbox_button_y
        case SDL_GAMEPAD_BUTTON_BACK:            return { "View",          0xE01C }; // xbox_button_view
        case SDL_GAMEPAD_BUTTON_GUIDE:           return { "Guide",         0xE041 }; // xbox_guide
        case SDL_GAMEPAD_BUTTON_START:           return { "Menu",          0xE014 }; // xbox_button_menu
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return { "LS",            0xE045 }; // xbox_ls
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return { "RS",            0xE04B }; // xbox_rs
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return { "LB",            0xE043 }; // xbox_lb
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return { "RB",            0xE049 }; // xbox_rb
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return { "D-Pad Up",      0xE035 }; // xbox_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return { "D-Pad Down",    0xE024 }; // xbox_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return { "D-Pad Left",    0xE028 }; // xbox_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return { "D-Pad Right",   0xE02B }; // xbox_dpad_right
        default: break;
        }
        break;

    case FontFamily::PlayStation:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return { "Cross",         0xE049 }; // playstation_button_cross
        case SDL_GAMEPAD_BUTTON_EAST:            return { "Circle",        0xE03F }; // playstation_button_circle
        case SDL_GAMEPAD_BUTTON_WEST:            return { "Square",        0xE04F }; // playstation_button_square
        case SDL_GAMEPAD_BUTTON_NORTH:           return { "Triangle",      0xE051 }; // playstation_button_triangle
        case SDL_GAMEPAD_BUTTON_BACK:            return { "Share/Create",  0xE01C }; // playstation5_button_create (general)
        case SDL_GAMEPAD_BUTTON_GUIDE:           return { "PS",            0xE03D }; // playstation_button_analog
        case SDL_GAMEPAD_BUTTON_START:           return { "Options",       0xE022 }; // playstation5_button_options
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return { "L3",            0xE04B }; // playstation_button_l3
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return { "R3",            0xE04D }; // playstation_button_r3
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return { "L1",            0xE076 }; // playstation_trigger_l1
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return { "R1",            0xE07E }; // playstation_trigger_r1
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return { "D-Pad Up",      0xE05E }; // playstation_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return { "D-Pad Down",    0xE055 }; // playstation_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return { "D-Pad Left",    0xE059 }; // playstation_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return { "D-Pad Right",   0xE05C }; // playstation_dpad_right
        default: break;
        }
        break;

    case FontFamily::Switch:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return { "B",             0xE006 }; // switch_button_b
        case SDL_GAMEPAD_BUTTON_EAST:            return { "A",             0xE004 }; // switch_button_a
        case SDL_GAMEPAD_BUTTON_WEST:            return { "Y",             0xE01A }; // switch_button_y
        case SDL_GAMEPAD_BUTTON_NORTH:           return { "X",             0xE018 }; // switch_button_x
        case SDL_GAMEPAD_BUTTON_BACK:            return { "Minus",         0xE00C }; // switch_button_minus
        case SDL_GAMEPAD_BUTTON_GUIDE:           return { "Home",          0xE008 }; // switch_button_home
        case SDL_GAMEPAD_BUTTON_START:           return { "Plus",          0xE00E }; // switch_button_plus
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return { "L Stick Press", 0xE05E }; // switch_stick_l_press
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return { "R Stick Press", 0xE066 }; // switch_stick_r_press
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return { "L",             0xE00A }; // switch_button_l
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return { "R",             0xE010 }; // switch_button_r
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return { "D-Pad Up",      0xE03C }; // switch_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return { "D-Pad Down",    0xE033 }; // switch_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return { "D-Pad Left",    0xE037 }; // switch_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return { "D-Pad Right",   0xE03A }; // switch_dpad_right
        default: break;
        }
        break;

    case FontFamily::SteamDeck:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return { "A",             0xE001 }; // steamdeck_button_a
        case SDL_GAMEPAD_BUTTON_EAST:            return { "B",             0xE003 }; // steamdeck_button_b
        case SDL_GAMEPAD_BUTTON_WEST:            return { "X",             0xE01D }; // steamdeck_button_x
        case SDL_GAMEPAD_BUTTON_NORTH:           return { "Y",             0xE01F }; // steamdeck_button_y
        case SDL_GAMEPAD_BUTTON_BACK:            return { "View",          0xE01B }; // steamdeck_button_view
        case SDL_GAMEPAD_BUTTON_GUIDE:           return { "Guide",         0xE005 }; // steamdeck_button_guide
        case SDL_GAMEPAD_BUTTON_START:           return { "Options",       0xE00F }; // steamdeck_button_options
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:      return { "L Stick Press", 0xE034 }; // steamdeck_stick_l_press
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:     return { "R Stick Press", 0xE03C }; // steamdeck_stick_r_press
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return { "L1",            0xE007 }; // steamdeck_button_l1
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return { "R1",            0xE013 }; // steamdeck_button_r1
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return { "D-Pad Up",      0xE02C }; // steamdeck_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return { "D-Pad Down",    0xE023 }; // steamdeck_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return { "D-Pad Left",    0xE027 }; // steamdeck_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return { "D-Pad Right",   0xE02A }; // steamdeck_dpad_right
        default: break;
        }
        break;

    case FontFamily::SteamController:
        switch (gb) {
        case SDL_GAMEPAD_BUTTON_SOUTH:           return { "A",             0xE022 }; // steam_button_a
        case SDL_GAMEPAD_BUTTON_EAST:            return { "B",             0xE024 }; // steam_button_b
        case SDL_GAMEPAD_BUTTON_WEST:            return { "X",             0xE036 }; // steam_button_x
        case SDL_GAMEPAD_BUTTON_NORTH:           return { "Y",             0xE038 }; // steam_button_y
        case SDL_GAMEPAD_BUTTON_BACK:            return { "Back",          0xE026 }; // steam_button_back_icon
        case SDL_GAMEPAD_BUTTON_GUIDE:           return { "Steam",         0xE016 }; // controller_icon
        case SDL_GAMEPAD_BUTTON_START:           return { "Start",         0xE034 }; // steam_button_start_icon
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:   return { "LB",            0xE049 }; // steam_lb
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  return { "RB",            0xE055 }; // steam_rb
        case SDL_GAMEPAD_BUTTON_DPAD_UP:         return { "D-Pad Up",      0xE045 }; // steam_dpad_up
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:       return { "D-Pad Down",    0xE03C }; // steam_dpad_down
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:       return { "D-Pad Left",    0xE040 }; // steam_dpad_left
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:      return { "D-Pad Right",   0xE043 }; // steam_dpad_right
        default: break;
        }
        break;

    default:
        break;
    }

    // Generic fallback for unknown family
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

// Chooses the correct icon font for generic-family input icons.
// For known families the device font IS the right font.
// For unknown families we fall back to the generic Kenney font.
ImFont* InputFont(const DeviceState& dev, FontFamily fam)
{
    ImFont* devFont = DeviceFont(dev);
    if (fam != FontFamily::Unknown && devFont) return devFont;
    return KenneyFonts::Get().generic;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// InputLabelProvider — public API
// ---------------------------------------------------------------------------

InputLabel InputLabelProvider::GetAxisLabel(const DeviceState& dev, int axis)
{
    InputLabel result;

    if (dev.gamepad)
    {
        // Walk every SDL_GamepadAxis and check whether its binding maps to
        // this joystick axis.
        const FontFamily fam  = GetFontFamily(dev);
        ImFont*          font = InputFont(dev, fam);

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
                        result.icon = MakeIcon(font, info.cp);
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

    if (dev.gamepad)
    {
        const FontFamily fam  = GetFontFamily(dev);
        ImFont*          font = InputFont(dev, fam);

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
                        result.icon = MakeIcon(font, info.cp);
                        SDL_free(bindings);
                        return result;
                    }
                }
            }
            SDL_free(bindings);
        }

        // Extra / unmapped button — numbered fallback.
        result.name = "Button " + std::to_string(button);
        result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE000); // generic_button
        return result;
    }

    // Non-gamepad path.
    result.name = "Button " + std::to_string(button);
    result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE000); // generic_button
    return result;
}

InputLabel InputLabelProvider::GetHatLabel(const DeviceState& dev, int hat)
{
    InputLabel result;
    result.name = "Hat " + std::to_string(hat);

    // Hats have no SDL gamepad binding equivalent (SDL maps them to dpad
    // buttons internally); use the generic joystick icon.
    (void)dev;
    result.icon = MakeIcon(KenneyFonts::Get().generic, 0xE013); // generic_joystick
    return result;
}
