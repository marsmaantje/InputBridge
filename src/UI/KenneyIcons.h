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
// Every icon is defined as a pair:
//   KENNEY_<NAME>_CP   — raw Unicode codepoint (ImWchar), for ImFontBaked::FindGlyphNoFallback()
//   KENNEY_<NAME>      — UTF-8 string literal,            for ImDrawList::AddText()
//
// The two values always refer to the same glyph and are kept in the same place
// so they can never drift out of sync.

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
// UTF-8 encoding helper
//
// All Kenney codepoints live in the BMP Private Use Area (U+E000-U+F8FF),
// which encodes as 3-byte UTF-8.  KENNEY_ICON_UTF8(cp) converts a codepoint
// constant into the corresponding string literal at compile time.
//
// Formula for any U+EXYZ in this range:
//   byte1 = 0xEE  (constant for U+E000-U+EFFF)
//   byte2 = 0x80 | ((cp >> 6) & 0x3F)
//   byte3 = 0x80 | (cp & 0x3F)
//
// Because the preprocessor cannot stringify computed hex values, the bytes
// are pre-computed per icon and written as explicit escape sequences.
// KENNEY_ICON_UTF8 is provided for documentation purposes; the per-icon
// string macros below are the authoritative UTF-8 form.
#define KENNEY_ICON_UTF8(cp) /* see per-icon _CP / string pairs below */

// ---------------------------------------------------------------------------
// Xbox icons  (kenney_input_xbox_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_XBOX_CONTROLLER_360_CP     0xE000
#define KENNEY_XBOX_CONTROLLER_360        "\xEE\x80\x80"  // U+E000  controller_xbox360

#define KENNEY_XBOX_CONTROLLER_ONE_CP     0xE002
#define KENNEY_XBOX_CONTROLLER_ONE        "\xEE\x80\x82"  // U+E002  controller_xboxone

#define KENNEY_XBOX_CONTROLLER_SERIES_CP  0xE003
#define KENNEY_XBOX_CONTROLLER_SERIES     "\xEE\x80\x83"  // U+E003  controller_xboxseries

// ---------------------------------------------------------------------------
// PlayStation icons  (kenney_input_playstation_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_PS_CONTROLLER_PS4_CP  0xE003
#define KENNEY_PS_CONTROLLER_PS4     "\xEE\x80\x83"  // U+E003  controller_playstation4

#define KENNEY_PS_CONTROLLER_PS5_CP  0xE004
#define KENNEY_PS_CONTROLLER_PS5     "\xEE\x80\x84"  // U+E004  controller_playstation5

// ---------------------------------------------------------------------------
// Nintendo Switch icons  (kenney_input_nintendo_switch.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_SWITCH_CONTROLLER_CP      0xE000
#define KENNEY_SWITCH_CONTROLLER         "\xEE\x80\x80"  // U+E000  controller_switch

#define KENNEY_SWITCH_CONTROLLER_PRO_CP  0xE003
#define KENNEY_SWITCH_CONTROLLER_PRO     "\xEE\x80\x83"  // U+E003  controller_switch_pro

#define KENNEY_SWITCH_JOYCON_DOWN_CP     0xE001
#define KENNEY_SWITCH_JOYCON_DOWN        "\xEE\x80\x81"  // U+E001  controller_switch_joycon_down

#define KENNEY_SWITCH_JOYCON_UP_CP       0xE002
#define KENNEY_SWITCH_JOYCON_UP          "\xEE\x80\x82"  // U+E002  controller_switch_joycon_up

// ---------------------------------------------------------------------------
// Nintendo Wii icons  (kenney_input_nintendo_wii.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_WII_CONTROLLER_CLASSIC_CP  0xE000
#define KENNEY_WII_CONTROLLER_CLASSIC     "\xEE\x80\x80"  // U+E000  controller_wii_classic

#define KENNEY_WII_CONTROLLER_CP          0xE022
#define KENNEY_WII_CONTROLLER             "\xEE\x80\xA2"  // U+E022  wii_controller

// ---------------------------------------------------------------------------
// Nintendo GameCube icons  (kenney_input_nintendo_gamecube.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_GC_CONTROLLER_CP  0xE014
#define KENNEY_GC_CONTROLLER     "\xEE\x80\x94"  // U+E014  gamecube_controller

// ---------------------------------------------------------------------------
// Keyboard & Mouse icons  (kenney_input_keyboard_&_mouse.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_KBM_KEYBOARD_CP  0xE000
#define KENNEY_KBM_KEYBOARD     "\xEE\x80\x80"  // U+E000  keyboard

#define KENNEY_KBM_MOUSE_CP     0xE0E9
#define KENNEY_KBM_MOUSE        "\xEE\x83\xA9"  // U+E0E9  mouse

// ---------------------------------------------------------------------------
// Steam Deck icons  (kenney_input_steam_deck.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAMDECK_CONTROLLER_CP  0xE000
#define KENNEY_STEAMDECK_CONTROLLER     "\xEE\x80\x80"  // U+E000  controller_steamdeck

// ---------------------------------------------------------------------------
// Steam Controller icons  (kenney_input_steam_controller.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAM_CONTROLLER_ICON_CP  0xE016
#define KENNEY_STEAM_CONTROLLER_ICON     "\xEE\x80\x96"  // U+E016  controller_icon

#define KENNEY_STEAM_CONTROLLER_CP       0xE020
#define KENNEY_STEAM_CONTROLLER          "\xEE\x80\xA0"  // U+E020  controller_steam

// ---------------------------------------------------------------------------
// Generic icons  (kenney_input_generic.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_GENERIC_JOYSTICK_CP  0xE013
#define KENNEY_GENERIC_JOYSTICK     "\xEE\x80\x93"  // U+E013  generic_joystick
