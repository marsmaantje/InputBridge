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
// All Kenney codepoints live in the BMP Private Use Area (U+E000–U+F8FF).
// That block encodes as 3-byte UTF-8:  0xE? → 0xEE or 0xEF prefix byte.
//
//   U+E0XX  →  0xEE 0x80+hi 0x80+lo
//   U+E1XX  →  0xEE 0x84+hi 0x80+lo   (etc.)
//
// The macro below builds a null-terminated string literal from any code in
// that range.  It is evaluated at compile time (all operands are constants).
//
// Usage:  KENNEY_ICON(0xE000)   →   "\xEE\x80\x80"
//         KENNEY_ICON(0xE016)   →   "\xEE\x80\x96"
//
#define _KI_B1(cp)  (0xE0 | (((cp) >> 12) & 0x0F))
#define _KI_B2(cp)  (0x80 | (((cp) >>  6) & 0x3F))
#define _KI_B3(cp)  (0x80 | (((cp)      ) & 0x3F))
#define KENNEY_ICON(cp) \
    "\x" _KI_STR(_KI_B1(cp)) "\x" _KI_STR(_KI_B2(cp)) "\x" _KI_STR(_KI_B3(cp))

// Because C preprocessor cannot stringify hex arithmetic results directly,
// we provide pre-computed string literals for every codepoint we actually use.
// Format:  \xEE\x8X\xYZ  where the first two bytes encode the upper bits and
// the third byte encodes the lower 6 bits.
//
// Formula for U+EABC:
//   byte1 = 0xEE (all our codes are < U+F000, so upper nibble is always 0xE)
//   byte2 = 0x80 | ((cp >> 6) & 0x3F)
//   byte3 = 0x80 | (cp & 0x3F)

// ---------------------------------------------------------------------------
// Xbox icons  (kenney_input_xbox_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_XBOX_CONTROLLER_360     "\xEE\x80\x80"  // U+E000  controller_xbox360
#define KENNEY_XBOX_CONTROLLER_ONE     "\xEE\x80\x82"  // U+E002  controller_xboxone
#define KENNEY_XBOX_CONTROLLER_SERIES  "\xEE\x80\x83"  // U+E003  controller_xboxseries

// ---------------------------------------------------------------------------
// PlayStation icons  (kenney_input_playstation_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_PS_CONTROLLER_PS4  "\xEE\x80\x83"  // U+E003  controller_playstation4
#define KENNEY_PS_CONTROLLER_PS5  "\xEE\x80\x84"  // U+E004  controller_playstation5

// ---------------------------------------------------------------------------
// Nintendo Switch icons  (kenney_input_nintendo_switch.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_SWITCH_CONTROLLER      "\xEE\x80\x80"  // U+E000  controller_switch
#define KENNEY_SWITCH_CONTROLLER_PRO  "\xEE\x80\x83"  // U+E003  controller_switch_pro
#define KENNEY_SWITCH_JOYCON_DOWN     "\xEE\x80\x81"  // U+E001  controller_switch_joycon_down
#define KENNEY_SWITCH_JOYCON_UP       "\xEE\x80\x82"  // U+E002  controller_switch_joycon_up

// ---------------------------------------------------------------------------
// Nintendo Wii icons  (kenney_input_nintendo_wii.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_WII_CONTROLLER_CLASSIC "\xEE\x80\x80"  // U+E000  controller_wii_classic
#define KENNEY_WII_CONTROLLER         "\xEE\x80\xA2"  // U+E022  wii_controller

// ---------------------------------------------------------------------------
// Nintendo GameCube icons  (kenney_input_nintendo_gamecube.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_GC_CONTROLLER  "\xEE\x80\x94"  // U+E014  gamecube_controller

// ---------------------------------------------------------------------------
// Keyboard & Mouse icons  (kenney_input_keyboard_&_mouse.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_KBM_KEYBOARD  "\xEE\x80\x80"  // U+E000  keyboard
#define KENNEY_KBM_MOUSE     "\xEE\x83\xA9"  // U+E0E9  mouse

// ---------------------------------------------------------------------------
// Steam Deck icons  (kenney_input_steam_deck.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAMDECK_CONTROLLER  "\xEE\x80\x80"  // U+E000  controller_steamdeck

// ---------------------------------------------------------------------------
// Steam Controller icons  (kenney_input_steam_controller.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAM_CONTROLLER_ICON  "\xEE\x80\x96"  // U+E016  controller_icon
#define KENNEY_STEAM_CONTROLLER       "\xEE\x80\xA0"  // U+E020  controller_steam

// ---------------------------------------------------------------------------
// Generic icons  (kenney_input_generic.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_GENERIC_JOYSTICK  "\xEE\x80\x93"  // U+E013  generic_joystick
