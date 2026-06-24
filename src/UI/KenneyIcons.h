#pragma once
// KenneyIcons.h
// Codepoints and UTF-8 string literals for the Kenney Input Prompt icon fonts.
//
// Each Kenney font uses overlapping Private Use Area codepoints starting at
// U+E000.  Because ImGui cannot remap codepoints on load, each font must be
// kept as a separate ImFont* and pushed with ImGui::PushFont() before use.
//
// Font files are expected next to the executable inside:
//   fonts/Kenney/<SubFolder>/<name>.ttf
//
// The map .txt files next to each font list every glyph name and its
// codepoint.  Use them to look up the U+XXXX value when adding a new icon,
// then define a _CP constant here.  The UTF-8 string is derived automatically
// by KenneyIconUTF8() below — no manual byte calculation needed.
//
// Every icon is defined as a pair:
//   KENNEY_<NAME>_CP  — raw Unicode codepoint (ImWchar), hand-picked from the map file
//   KENNEY_<NAME>     — UTF-8 string literal,            computed by KenneyIconUTF8()

#include "imgui.h"
#include <array>

// ---------------------------------------------------------------------------
// KenneyIconUTF8
//
// Converts a Private Use Area codepoint (U+E000-U+F8FF) to a 3-byte UTF-8
// std::array at compile time.  Returns a null-terminated char array that can
// be passed directly to ImDrawList::AddText().
//
// All codepoints in this range encode as:
//   byte 0 = 0xE0 | (cp >> 12)        — always 0xEE for U+E000-U+EFFF
//   byte 1 = 0x80 | ((cp >> 6) & 0x3F)
//   byte 2 = 0x80 | (cp & 0x3F)
//   byte 3 = 0x00                      — null terminator
//
// Usage:
//   static constexpr auto buf = KenneyIconUTF8(KENNEY_STEAM_CONTROLLER_CP);
//   draw_list->AddText(..., buf.data());
//
// The convenience macros KENNEY_<NAME> wrap this so call sites look identical
// to a plain string literal.
// ---------------------------------------------------------------------------
constexpr std::array<char, 4> KenneyIconUTF8(ImWchar cp)
{
    return {{
        static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)),
        static_cast<char>(0x80 | ((cp >>  6) & 0x3F)),
        static_cast<char>(0x80 | ((cp      ) & 0x3F)),
        '\0'
    }};
}

// Convenience macro: declares a static constexpr buffer and evaluates to a
// const char* pointing to it.  Safe to use anywhere a string literal is
// expected, including as a function argument.
#define KENNEY_ICON_STR(cp) \
    ([]() -> const char* { \
        static constexpr auto _buf = KenneyIconUTF8(cp); \
        return _buf.data(); \
    }())

// Sub-folder constants -------------------------------------------------------
#define KENNEY_DIR_XBOX           "Kenney/Xbox/"
#define KENNEY_DIR_PLAYSTATION    "Kenney/PlayStation/"
#define KENNEY_DIR_SWITCH         "Kenney/NintendoSwitch/"
#define KENNEY_DIR_WII            "Kenney/NintendoWii/"
#define KENNEY_DIR_GAMECUBE       "Kenney/NintendoGamecube/"
#define KENNEY_DIR_KEYBOARD_MOUSE "Kenney/KeyboardMouse/"
#define KENNEY_DIR_STEAM_DECK     "Kenney/SteamDeck/"
#define KENNEY_DIR_STEAM_CTRL     "Kenney/SteamController/"
#define KENNEY_DIR_GENERIC        "Kenney/Generic/"

// Font file names ------------------------------------------------------------
#define KENNEY_FILE_XBOX           "kenney_input_xbox_series.ttf"
#define KENNEY_FILE_PLAYSTATION    "kenney_input_playstation_series.ttf"
#define KENNEY_FILE_SWITCH         "kenney_input_nintendo_switch.ttf"
#define KENNEY_FILE_WII            "kenney_input_nintendo_wii.ttf"
#define KENNEY_FILE_GAMECUBE       "kenney_input_nintendo_gamecube.ttf"
#define KENNEY_FILE_KEYBOARD_MOUSE "kenney_input_keyboard_&_mouse.ttf"
#define KENNEY_FILE_STEAM_DECK     "kenney_input_steam_deck.ttf"
#define KENNEY_FILE_STEAM_CTRL     "kenney_input_steam_controller.ttf"
#define KENNEY_FILE_GENERIC        "kenney_input_generic.ttf"

// ---------------------------------------------------------------------------
// Xbox icons  (kenney_input_xbox_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_XBOX_CONTROLLER_360_CP     0xE000  // controller_xbox360
#define KENNEY_XBOX_CONTROLLER_360        KENNEY_ICON_STR(KENNEY_XBOX_CONTROLLER_360_CP)

#define KENNEY_XBOX_CONTROLLER_ONE_CP     0xE002  // controller_xboxone
#define KENNEY_XBOX_CONTROLLER_ONE        KENNEY_ICON_STR(KENNEY_XBOX_CONTROLLER_ONE_CP)

#define KENNEY_XBOX_CONTROLLER_SERIES_CP  0xE003  // controller_xboxseries
#define KENNEY_XBOX_CONTROLLER_SERIES     KENNEY_ICON_STR(KENNEY_XBOX_CONTROLLER_SERIES_CP)

// ---------------------------------------------------------------------------
// PlayStation icons  (kenney_input_playstation_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_PS_CONTROLLER_PS4_CP  0xE003  // controller_playstation4
#define KENNEY_PS_CONTROLLER_PS4     KENNEY_ICON_STR(KENNEY_PS_CONTROLLER_PS4_CP)

#define KENNEY_PS_CONTROLLER_PS5_CP  0xE004  // controller_playstation5
#define KENNEY_PS_CONTROLLER_PS5     KENNEY_ICON_STR(KENNEY_PS_CONTROLLER_PS5_CP)

// ---------------------------------------------------------------------------
// Nintendo Switch icons  (kenney_input_nintendo_switch.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_SWITCH_CONTROLLER_CP      0xE000  // controller_switch
#define KENNEY_SWITCH_CONTROLLER         KENNEY_ICON_STR(KENNEY_SWITCH_CONTROLLER_CP)

#define KENNEY_SWITCH_CONTROLLER_PRO_CP  0xE003  // controller_switch_pro
#define KENNEY_SWITCH_CONTROLLER_PRO     KENNEY_ICON_STR(KENNEY_SWITCH_CONTROLLER_PRO_CP)

#define KENNEY_SWITCH_JOYCON_DOWN_CP     0xE001  // controller_switch_joycon_down
#define KENNEY_SWITCH_JOYCON_DOWN        KENNEY_ICON_STR(KENNEY_SWITCH_JOYCON_DOWN_CP)

#define KENNEY_SWITCH_JOYCON_UP_CP       0xE002  // controller_switch_joycon_up
#define KENNEY_SWITCH_JOYCON_UP          KENNEY_ICON_STR(KENNEY_SWITCH_JOYCON_UP_CP)

// ---------------------------------------------------------------------------
// Nintendo Wii icons  (kenney_input_nintendo_wii.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_WII_CONTROLLER_CLASSIC_CP  0xE000  // controller_wii_classic
#define KENNEY_WII_CONTROLLER_CLASSIC     KENNEY_ICON_STR(KENNEY_WII_CONTROLLER_CLASSIC_CP)

#define KENNEY_WII_CONTROLLER_CP          0xE022  // wii_controller
#define KENNEY_WII_CONTROLLER             KENNEY_ICON_STR(KENNEY_WII_CONTROLLER_CP)

// ---------------------------------------------------------------------------
// Nintendo GameCube icons  (kenney_input_nintendo_gamecube.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_GC_CONTROLLER_CP  0xE014  // gamecube_controller
#define KENNEY_GC_CONTROLLER     KENNEY_ICON_STR(KENNEY_GC_CONTROLLER_CP)

// ---------------------------------------------------------------------------
// Keyboard & Mouse icons  (kenney_input_keyboard_&_mouse.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_KBM_KEYBOARD_CP  0xE000  // keyboard
#define KENNEY_KBM_KEYBOARD     KENNEY_ICON_STR(KENNEY_KBM_KEYBOARD_CP)

#define KENNEY_KBM_MOUSE_CP     0xE0E9  // mouse
#define KENNEY_KBM_MOUSE        KENNEY_ICON_STR(KENNEY_KBM_MOUSE_CP)

// ---------------------------------------------------------------------------
// Steam Deck icons  (kenney_input_steam_deck.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAMDECK_CONTROLLER_CP  0xE000  // controller_steamdeck
#define KENNEY_STEAMDECK_CONTROLLER     KENNEY_ICON_STR(KENNEY_STEAMDECK_CONTROLLER_CP)

// ---------------------------------------------------------------------------
// Steam Controller icons  (kenney_input_steam_controller.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAM_CONTROLLER_CP       0xE020  // controller_steam
#define KENNEY_STEAM_CONTROLLER          KENNEY_ICON_STR(KENNEY_STEAM_CONTROLLER_CP)

#define KENNEY_STEAM_CONTROLLER_NEW_CP   0xE021  // controller_steam_new
#define KENNEY_STEAM_CONTROLLER_NEW      KENNEY_ICON_STR(KENNEY_STEAM_CONTROLLER_NEW_CP)

// ---------------------------------------------------------------------------
// Generic icons  (kenney_input_generic.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_GENERIC_JOYSTICK_CP  0xE013  // generic_joystick
#define KENNEY_GENERIC_JOYSTICK     KENNEY_ICON_STR(KENNEY_GENERIC_JOYSTICK_CP)
