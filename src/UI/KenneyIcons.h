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
#define KENNEY_GENERIC_BUTTON_CP              0xE000  // generic_button
#define KENNEY_GENERIC_BUTTON                 KENNEY_ICON_STR(KENNEY_GENERIC_BUTTON_CP)

#define KENNEY_GENERIC_JOYSTICK_CP            0xE013  // generic_joystick
#define KENNEY_GENERIC_JOYSTICK               KENNEY_ICON_STR(KENNEY_GENERIC_JOYSTICK_CP)

#define KENNEY_GENERIC_STICK_CP               0xE01B  // generic_stick
#define KENNEY_GENERIC_STICK                  KENNEY_ICON_STR(KENNEY_GENERIC_STICK_CP)

#define KENNEY_GENERIC_STICK_HORIZONTAL_CP    0xE01D  // generic_stick_horizontal
#define KENNEY_GENERIC_STICK_HORIZONTAL       KENNEY_ICON_STR(KENNEY_GENERIC_STICK_HORIZONTAL_CP)

#define KENNEY_GENERIC_STICK_PRESS_CP         0xE01F  // generic_stick_press
#define KENNEY_GENERIC_STICK_PRESS            KENNEY_ICON_STR(KENNEY_GENERIC_STICK_PRESS_CP)

#define KENNEY_GENERIC_STICK_VERTICAL_CP      0xE023  // generic_stick_vertical
#define KENNEY_GENERIC_STICK_VERTICAL         KENNEY_ICON_STR(KENNEY_GENERIC_STICK_VERTICAL_CP)

// ---------------------------------------------------------------------------
// Xbox per-input icons  (kenney_input_xbox_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_XBOX_BUTTON_A_CP               0xE004  // xbox_button_a
#define KENNEY_XBOX_BUTTON_A                  KENNEY_ICON_STR(KENNEY_XBOX_BUTTON_A_CP)
#define KENNEY_XBOX_BUTTON_B_CP               0xE006  // xbox_button_b
#define KENNEY_XBOX_BUTTON_B                  KENNEY_ICON_STR(KENNEY_XBOX_BUTTON_B_CP)
#define KENNEY_XBOX_BUTTON_X_CP               0xE01E  // xbox_button_x
#define KENNEY_XBOX_BUTTON_X                  KENNEY_ICON_STR(KENNEY_XBOX_BUTTON_X_CP)
#define KENNEY_XBOX_BUTTON_Y_CP               0xE020  // xbox_button_y
#define KENNEY_XBOX_BUTTON_Y                  KENNEY_ICON_STR(KENNEY_XBOX_BUTTON_Y_CP)
#define KENNEY_XBOX_BUTTON_VIEW_CP            0xE01C  // xbox_button_view
#define KENNEY_XBOX_BUTTON_VIEW               KENNEY_ICON_STR(KENNEY_XBOX_BUTTON_VIEW_CP)
#define KENNEY_XBOX_BUTTON_MENU_CP            0xE014  // xbox_button_menu
#define KENNEY_XBOX_BUTTON_MENU               KENNEY_ICON_STR(KENNEY_XBOX_BUTTON_MENU_CP)
#define KENNEY_XBOX_GUIDE_CP                  0xE041  // xbox_guide
#define KENNEY_XBOX_GUIDE                     KENNEY_ICON_STR(KENNEY_XBOX_GUIDE_CP)
#define KENNEY_XBOX_LB_CP                     0xE043  // xbox_lb
#define KENNEY_XBOX_LB                        KENNEY_ICON_STR(KENNEY_XBOX_LB_CP)
#define KENNEY_XBOX_RB_CP                     0xE049  // xbox_rb
#define KENNEY_XBOX_RB                        KENNEY_ICON_STR(KENNEY_XBOX_RB_CP)
#define KENNEY_XBOX_LT_CP                     0xE047  // xbox_lt
#define KENNEY_XBOX_LT                        KENNEY_ICON_STR(KENNEY_XBOX_LT_CP)
#define KENNEY_XBOX_RT_CP                     0xE04D  // xbox_rt
#define KENNEY_XBOX_RT                        KENNEY_ICON_STR(KENNEY_XBOX_RT_CP)
#define KENNEY_XBOX_LS_CP                     0xE045  // xbox_ls
#define KENNEY_XBOX_LS                        KENNEY_ICON_STR(KENNEY_XBOX_LS_CP)
#define KENNEY_XBOX_RS_CP                     0xE04B  // xbox_rs
#define KENNEY_XBOX_RS                        KENNEY_ICON_STR(KENNEY_XBOX_RS_CP)
#define KENNEY_XBOX_DPAD_UP_CP                0xE035  // xbox_dpad_up
#define KENNEY_XBOX_DPAD_UP                   KENNEY_ICON_STR(KENNEY_XBOX_DPAD_UP_CP)
#define KENNEY_XBOX_DPAD_DOWN_CP              0xE024  // xbox_dpad_down
#define KENNEY_XBOX_DPAD_DOWN                 KENNEY_ICON_STR(KENNEY_XBOX_DPAD_DOWN_CP)
#define KENNEY_XBOX_DPAD_LEFT_CP              0xE028  // xbox_dpad_left
#define KENNEY_XBOX_DPAD_LEFT                 KENNEY_ICON_STR(KENNEY_XBOX_DPAD_LEFT_CP)
#define KENNEY_XBOX_DPAD_RIGHT_CP             0xE02B  // xbox_dpad_right
#define KENNEY_XBOX_DPAD_RIGHT                KENNEY_ICON_STR(KENNEY_XBOX_DPAD_RIGHT_CP)
#define KENNEY_XBOX_STICK_L_HORIZONTAL_CP     0xE051  // xbox_stick_l_horizontal
#define KENNEY_XBOX_STICK_L_HORIZONTAL        KENNEY_ICON_STR(KENNEY_XBOX_STICK_L_HORIZONTAL_CP)
#define KENNEY_XBOX_STICK_L_VERTICAL_CP       0xE056  // xbox_stick_l_vertical
#define KENNEY_XBOX_STICK_L_VERTICAL          KENNEY_ICON_STR(KENNEY_XBOX_STICK_L_VERTICAL_CP)
#define KENNEY_XBOX_STICK_R_HORIZONTAL_CP     0xE059  // xbox_stick_r_horizontal
#define KENNEY_XBOX_STICK_R_HORIZONTAL        KENNEY_ICON_STR(KENNEY_XBOX_STICK_R_HORIZONTAL_CP)
#define KENNEY_XBOX_STICK_R_VERTICAL_CP       0xE05E  // xbox_stick_r_vertical
#define KENNEY_XBOX_STICK_R_VERTICAL          KENNEY_ICON_STR(KENNEY_XBOX_STICK_R_VERTICAL_CP)

// ---------------------------------------------------------------------------
// PlayStation per-input icons  (kenney_input_playstation_series.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_PS_BUTTON_CROSS_CP             0xE049  // playstation_button_cross
#define KENNEY_PS_BUTTON_CROSS                KENNEY_ICON_STR(KENNEY_PS_BUTTON_CROSS_CP)
#define KENNEY_PS_BUTTON_CIRCLE_CP            0xE03F  // playstation_button_circle
#define KENNEY_PS_BUTTON_CIRCLE               KENNEY_ICON_STR(KENNEY_PS_BUTTON_CIRCLE_CP)
#define KENNEY_PS_BUTTON_SQUARE_CP            0xE04F  // playstation_button_square
#define KENNEY_PS_BUTTON_SQUARE               KENNEY_ICON_STR(KENNEY_PS_BUTTON_SQUARE_CP)
#define KENNEY_PS_BUTTON_TRIANGLE_CP          0xE051  // playstation_button_triangle
#define KENNEY_PS_BUTTON_TRIANGLE             KENNEY_ICON_STR(KENNEY_PS_BUTTON_TRIANGLE_CP)
#define KENNEY_PS_BUTTON_L1_CP                0xE076  // playstation_trigger_l1
#define KENNEY_PS_BUTTON_L1                   KENNEY_ICON_STR(KENNEY_PS_BUTTON_L1_CP)
#define KENNEY_PS_BUTTON_R1_CP                0xE07E  // playstation_trigger_r1
#define KENNEY_PS_BUTTON_R1                   KENNEY_ICON_STR(KENNEY_PS_BUTTON_R1_CP)
#define KENNEY_PS_TRIGGER_L2_CP               0xE07A  // playstation_trigger_l2
#define KENNEY_PS_TRIGGER_L2                  KENNEY_ICON_STR(KENNEY_PS_TRIGGER_L2_CP)
#define KENNEY_PS_TRIGGER_R2_CP               0xE082  // playstation_trigger_r2
#define KENNEY_PS_TRIGGER_R2                  KENNEY_ICON_STR(KENNEY_PS_TRIGGER_R2_CP)
#define KENNEY_PS_BUTTON_L3_CP                0xE04B  // playstation_button_l3
#define KENNEY_PS_BUTTON_L3                   KENNEY_ICON_STR(KENNEY_PS_BUTTON_L3_CP)
#define KENNEY_PS_BUTTON_R3_CP                0xE04D  // playstation_button_r3
#define KENNEY_PS_BUTTON_R3                   KENNEY_ICON_STR(KENNEY_PS_BUTTON_R3_CP)
#define KENNEY_PS_BUTTON_OPTIONS_CP           0xE022  // playstation5_button_options
#define KENNEY_PS_BUTTON_OPTIONS              KENNEY_ICON_STR(KENNEY_PS_BUTTON_OPTIONS_CP)
#define KENNEY_PS_BUTTON_CREATE_CP            0xE01C  // playstation5_button_create
#define KENNEY_PS_BUTTON_CREATE               KENNEY_ICON_STR(KENNEY_PS_BUTTON_CREATE_CP)
#define KENNEY_PS_BUTTON_ANALOG_CP            0xE03D  // playstation_button_analog (PS logo)
#define KENNEY_PS_BUTTON_ANALOG               KENNEY_ICON_STR(KENNEY_PS_BUTTON_ANALOG_CP)
#define KENNEY_PS_DPAD_UP_CP                  0xE05E  // playstation_dpad_up
#define KENNEY_PS_DPAD_UP                     KENNEY_ICON_STR(KENNEY_PS_DPAD_UP_CP)
#define KENNEY_PS_DPAD_DOWN_CP                0xE055  // playstation_dpad_down
#define KENNEY_PS_DPAD_DOWN                   KENNEY_ICON_STR(KENNEY_PS_DPAD_DOWN_CP)
#define KENNEY_PS_DPAD_LEFT_CP                0xE059  // playstation_dpad_left
#define KENNEY_PS_DPAD_LEFT                   KENNEY_ICON_STR(KENNEY_PS_DPAD_LEFT_CP)
#define KENNEY_PS_DPAD_RIGHT_CP               0xE05C  // playstation_dpad_right
#define KENNEY_PS_DPAD_RIGHT                  KENNEY_ICON_STR(KENNEY_PS_DPAD_RIGHT_CP)
#define KENNEY_PS_STICK_L_HORIZONTAL_CP       0xE064  // playstation_stick_l_horizontal
#define KENNEY_PS_STICK_L_HORIZONTAL          KENNEY_ICON_STR(KENNEY_PS_STICK_L_HORIZONTAL_CP)
#define KENNEY_PS_STICK_L_VERTICAL_CP         0xE069  // playstation_stick_l_vertical
#define KENNEY_PS_STICK_L_VERTICAL            KENNEY_ICON_STR(KENNEY_PS_STICK_L_VERTICAL_CP)
#define KENNEY_PS_STICK_R_HORIZONTAL_CP       0xE06C  // playstation_stick_r_horizontal
#define KENNEY_PS_STICK_R_HORIZONTAL          KENNEY_ICON_STR(KENNEY_PS_STICK_R_HORIZONTAL_CP)
#define KENNEY_PS_STICK_R_VERTICAL_CP         0xE071  // playstation_stick_r_vertical
#define KENNEY_PS_STICK_R_VERTICAL            KENNEY_ICON_STR(KENNEY_PS_STICK_R_VERTICAL_CP)

// ---------------------------------------------------------------------------
// Nintendo Switch per-input icons  (kenney_input_nintendo_switch.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_SWITCH_BUTTON_A_CP             0xE004  // switch_button_a
#define KENNEY_SWITCH_BUTTON_A                KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_A_CP)
#define KENNEY_SWITCH_BUTTON_B_CP             0xE006  // switch_button_b
#define KENNEY_SWITCH_BUTTON_B                KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_B_CP)
#define KENNEY_SWITCH_BUTTON_X_CP             0xE018  // switch_button_x
#define KENNEY_SWITCH_BUTTON_X                KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_X_CP)
#define KENNEY_SWITCH_BUTTON_Y_CP             0xE01A  // switch_button_y
#define KENNEY_SWITCH_BUTTON_Y                KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_Y_CP)
#define KENNEY_SWITCH_BUTTON_HOME_CP          0xE008  // switch_button_home
#define KENNEY_SWITCH_BUTTON_HOME             KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_HOME_CP)
#define KENNEY_SWITCH_BUTTON_MINUS_CP         0xE00C  // switch_button_minus
#define KENNEY_SWITCH_BUTTON_MINUS            KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_MINUS_CP)
#define KENNEY_SWITCH_BUTTON_PLUS_CP          0xE00E  // switch_button_plus
#define KENNEY_SWITCH_BUTTON_PLUS             KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_PLUS_CP)
#define KENNEY_SWITCH_BUTTON_L_CP             0xE00A  // switch_button_l
#define KENNEY_SWITCH_BUTTON_L                KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_L_CP)
#define KENNEY_SWITCH_BUTTON_R_CP             0xE010  // switch_button_r
#define KENNEY_SWITCH_BUTTON_R                KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_R_CP)
#define KENNEY_SWITCH_BUTTON_ZL_CP            0xE01C  // switch_button_zl
#define KENNEY_SWITCH_BUTTON_ZL               KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_ZL_CP)
#define KENNEY_SWITCH_BUTTON_ZR_CP            0xE01E  // switch_button_zr
#define KENNEY_SWITCH_BUTTON_ZR               KENNEY_ICON_STR(KENNEY_SWITCH_BUTTON_ZR_CP)
#define KENNEY_SWITCH_STICK_L_PRESS_CP        0xE05E  // switch_stick_l_press
#define KENNEY_SWITCH_STICK_L_PRESS           KENNEY_ICON_STR(KENNEY_SWITCH_STICK_L_PRESS_CP)
#define KENNEY_SWITCH_STICK_R_PRESS_CP        0xE066  // switch_stick_r_press
#define KENNEY_SWITCH_STICK_R_PRESS           KENNEY_ICON_STR(KENNEY_SWITCH_STICK_R_PRESS_CP)
#define KENNEY_SWITCH_DPAD_UP_CP              0xE03C  // switch_dpad_up
#define KENNEY_SWITCH_DPAD_UP                 KENNEY_ICON_STR(KENNEY_SWITCH_DPAD_UP_CP)
#define KENNEY_SWITCH_DPAD_DOWN_CP            0xE033  // switch_dpad_down
#define KENNEY_SWITCH_DPAD_DOWN               KENNEY_ICON_STR(KENNEY_SWITCH_DPAD_DOWN_CP)
#define KENNEY_SWITCH_DPAD_LEFT_CP            0xE037  // switch_dpad_left
#define KENNEY_SWITCH_DPAD_LEFT               KENNEY_ICON_STR(KENNEY_SWITCH_DPAD_LEFT_CP)
#define KENNEY_SWITCH_DPAD_RIGHT_CP           0xE03A  // switch_dpad_right
#define KENNEY_SWITCH_DPAD_RIGHT              KENNEY_ICON_STR(KENNEY_SWITCH_DPAD_RIGHT_CP)
#define KENNEY_SWITCH_STICK_L_HORIZONTAL_CP   0xE05C  // switch_stick_l_horizontal
#define KENNEY_SWITCH_STICK_L_HORIZONTAL      KENNEY_ICON_STR(KENNEY_SWITCH_STICK_L_HORIZONTAL_CP)
#define KENNEY_SWITCH_STICK_L_VERTICAL_CP     0xE061  // switch_stick_l_vertical
#define KENNEY_SWITCH_STICK_L_VERTICAL        KENNEY_ICON_STR(KENNEY_SWITCH_STICK_L_VERTICAL_CP)
#define KENNEY_SWITCH_STICK_R_HORIZONTAL_CP   0xE064  // switch_stick_r_horizontal
#define KENNEY_SWITCH_STICK_R_HORIZONTAL      KENNEY_ICON_STR(KENNEY_SWITCH_STICK_R_HORIZONTAL_CP)
#define KENNEY_SWITCH_STICK_R_VERTICAL_CP     0xE069  // switch_stick_r_vertical
#define KENNEY_SWITCH_STICK_R_VERTICAL        KENNEY_ICON_STR(KENNEY_SWITCH_STICK_R_VERTICAL_CP)

// ---------------------------------------------------------------------------
// Steam Deck per-input icons  (kenney_input_steam_deck.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAMDECK_BUTTON_A_CP          0xE001  // steamdeck_button_a
#define KENNEY_STEAMDECK_BUTTON_A             KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_A_CP)
#define KENNEY_STEAMDECK_BUTTON_B_CP          0xE003  // steamdeck_button_b
#define KENNEY_STEAMDECK_BUTTON_B             KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_B_CP)
#define KENNEY_STEAMDECK_BUTTON_X_CP          0xE01D  // steamdeck_button_x
#define KENNEY_STEAMDECK_BUTTON_X             KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_X_CP)
#define KENNEY_STEAMDECK_BUTTON_Y_CP          0xE01F  // steamdeck_button_y
#define KENNEY_STEAMDECK_BUTTON_Y             KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_Y_CP)
#define KENNEY_STEAMDECK_BUTTON_VIEW_CP       0xE01B  // steamdeck_button_view
#define KENNEY_STEAMDECK_BUTTON_VIEW          KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_VIEW_CP)
#define KENNEY_STEAMDECK_BUTTON_GUIDE_CP      0xE005  // steamdeck_button_guide
#define KENNEY_STEAMDECK_BUTTON_GUIDE         KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_GUIDE_CP)
#define KENNEY_STEAMDECK_BUTTON_OPTIONS_CP    0xE00F  // steamdeck_button_options
#define KENNEY_STEAMDECK_BUTTON_OPTIONS       KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_OPTIONS_CP)
#define KENNEY_STEAMDECK_BUTTON_L1_CP         0xE007  // steamdeck_button_l1
#define KENNEY_STEAMDECK_BUTTON_L1            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_L1_CP)
#define KENNEY_STEAMDECK_BUTTON_R1_CP         0xE013  // steamdeck_button_r1
#define KENNEY_STEAMDECK_BUTTON_R1            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_R1_CP)
#define KENNEY_STEAMDECK_BUTTON_L2_CP         0xE009  // steamdeck_button_l2
#define KENNEY_STEAMDECK_BUTTON_L2            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_L2_CP)
#define KENNEY_STEAMDECK_BUTTON_R2_CP         0xE015  // steamdeck_button_r2
#define KENNEY_STEAMDECK_BUTTON_R2            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_R2_CP)
#define KENNEY_STEAMDECK_BUTTON_L4_CP         0xE00B  // steamdeck_button_l4 (back grip paddle)
#define KENNEY_STEAMDECK_BUTTON_L4            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_L4_CP)
#define KENNEY_STEAMDECK_BUTTON_L5_CP         0xE00D  // steamdeck_button_l5 (back grip paddle)
#define KENNEY_STEAMDECK_BUTTON_L5            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_L5_CP)
#define KENNEY_STEAMDECK_BUTTON_R4_CP         0xE017  // steamdeck_button_r4 (back grip paddle)
#define KENNEY_STEAMDECK_BUTTON_R4            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_R4_CP)
#define KENNEY_STEAMDECK_BUTTON_R5_CP         0xE019  // steamdeck_button_r5 (back grip paddle)
#define KENNEY_STEAMDECK_BUTTON_R5            KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_R5_CP)
#define KENNEY_STEAMDECK_BUTTON_QUICKACCESS_CP 0xE011 // steamdeck_button_quickaccess ("..." button)
#define KENNEY_STEAMDECK_BUTTON_QUICKACCESS   KENNEY_ICON_STR(KENNEY_STEAMDECK_BUTTON_QUICKACCESS_CP)
#define KENNEY_STEAMDECK_TRACKPAD_ALL_CP      0xE045  // steamdeck_trackpad_all (click)
#define KENNEY_STEAMDECK_TRACKPAD_ALL         KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_ALL_CP)
#define KENNEY_STEAMDECK_TRACKPAD_L_HORIZONTAL_CP 0xE050 // steamdeck_trackpad_l_horizontal
#define KENNEY_STEAMDECK_TRACKPAD_L_HORIZONTAL    KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_L_HORIZONTAL_CP)
#define KENNEY_STEAMDECK_TRACKPAD_L_VERTICAL_CP   0xE059 // steamdeck_trackpad_l_vertical
#define KENNEY_STEAMDECK_TRACKPAD_L_VERTICAL      KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_L_VERTICAL_CP)
#define KENNEY_STEAMDECK_TRACKPAD_R_HORIZONTAL_CP 0xE063 // steamdeck_trackpad_r_horizontal
#define KENNEY_STEAMDECK_TRACKPAD_R_HORIZONTAL    KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_R_HORIZONTAL_CP)
#define KENNEY_STEAMDECK_TRACKPAD_R_VERTICAL_CP   0xE06C // steamdeck_trackpad_r_vertical
#define KENNEY_STEAMDECK_TRACKPAD_R_VERTICAL      KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_R_VERTICAL_CP)
#define KENNEY_STEAMDECK_STICK_L_PRESS_CP     0xE034  // steamdeck_stick_l_press
#define KENNEY_STEAMDECK_STICK_L_PRESS        KENNEY_ICON_STR(KENNEY_STEAMDECK_STICK_L_PRESS_CP)
#define KENNEY_STEAMDECK_STICK_R_PRESS_CP     0xE03C  // steamdeck_stick_r_press
#define KENNEY_STEAMDECK_STICK_R_PRESS        KENNEY_ICON_STR(KENNEY_STEAMDECK_STICK_R_PRESS_CP)
#define KENNEY_STEAMDECK_DPAD_UP_CP           0xE02C  // steamdeck_dpad_up
#define KENNEY_STEAMDECK_DPAD_UP              KENNEY_ICON_STR(KENNEY_STEAMDECK_DPAD_UP_CP)
#define KENNEY_STEAMDECK_DPAD_DOWN_CP         0xE023  // steamdeck_dpad_down
#define KENNEY_STEAMDECK_DPAD_DOWN            KENNEY_ICON_STR(KENNEY_STEAMDECK_DPAD_DOWN_CP)
#define KENNEY_STEAMDECK_DPAD_LEFT_CP         0xE027  // steamdeck_dpad_left
#define KENNEY_STEAMDECK_DPAD_LEFT            KENNEY_ICON_STR(KENNEY_STEAMDECK_DPAD_LEFT_CP)
#define KENNEY_STEAMDECK_DPAD_RIGHT_CP        0xE02A  // steamdeck_dpad_right
#define KENNEY_STEAMDECK_DPAD_RIGHT           KENNEY_ICON_STR(KENNEY_STEAMDECK_DPAD_RIGHT_CP)
#define KENNEY_STEAMDECK_STICK_L_HORIZONTAL_CP 0xE032 // steamdeck_stick_l_horizontal
#define KENNEY_STEAMDECK_STICK_L_HORIZONTAL   KENNEY_ICON_STR(KENNEY_STEAMDECK_STICK_L_HORIZONTAL_CP)
#define KENNEY_STEAMDECK_STICK_L_VERTICAL_CP  0xE037  // steamdeck_stick_l_vertical
#define KENNEY_STEAMDECK_STICK_L_VERTICAL     KENNEY_ICON_STR(KENNEY_STEAMDECK_STICK_L_VERTICAL_CP)
#define KENNEY_STEAMDECK_STICK_R_HORIZONTAL_CP 0xE03A // steamdeck_stick_r_horizontal
#define KENNEY_STEAMDECK_STICK_R_HORIZONTAL   KENNEY_ICON_STR(KENNEY_STEAMDECK_STICK_R_HORIZONTAL_CP)
#define KENNEY_STEAMDECK_STICK_R_VERTICAL_CP  0xE03F  // steamdeck_stick_r_vertical
#define KENNEY_STEAMDECK_STICK_R_VERTICAL     KENNEY_ICON_STR(KENNEY_STEAMDECK_STICK_R_VERTICAL_CP)
#define KENNEY_STEAMDECK_DPAD_CP              0xE021  // steamdeck_dpad (neutral/centered)
#define KENNEY_STEAMDECK_DPAD                 KENNEY_ICON_STR(KENNEY_STEAMDECK_DPAD_CP)
#define KENNEY_STEAMDECK_TRACKPAD_L_ALL_CP    0xE04C  // steamdeck_trackpad_l_all (click anywhere)
#define KENNEY_STEAMDECK_TRACKPAD_L_ALL       KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_L_ALL_CP)
#define KENNEY_STEAMDECK_TRACKPAD_R_ALL_CP    0xE05F  // steamdeck_trackpad_r_all (click anywhere)
#define KENNEY_STEAMDECK_TRACKPAD_R_ALL       KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_R_ALL_CP)
#define KENNEY_STEAMDECK_TRACKPAD_L_CP        0xE04B  // steamdeck_trackpad_l
#define KENNEY_STEAMDECK_TRACKPAD_L           KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_L_CP)
#define KENNEY_STEAMDECK_TRACKPAD_R_CP        0xE05E  // steamdeck_trackpad_r
#define KENNEY_STEAMDECK_TRACKPAD_R           KENNEY_ICON_STR(KENNEY_STEAMDECK_TRACKPAD_R_CP)

// ---------------------------------------------------------------------------
// Steam Controller per-input icons  (kenney_input_steam_controller.ttf)
// ---------------------------------------------------------------------------
#define KENNEY_STEAM_BUTTON_A_CP              0xE022  // steam_button_a
#define KENNEY_STEAM_BUTTON_A                 KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_A_CP)
#define KENNEY_STEAM_BUTTON_B_CP              0xE024  // steam_button_b
#define KENNEY_STEAM_BUTTON_B                 KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_B_CP)
#define KENNEY_STEAM_BUTTON_X_CP              0xE036  // steam_button_x
#define KENNEY_STEAM_BUTTON_X                 KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_X_CP)
#define KENNEY_STEAM_BUTTON_Y_CP              0xE038  // steam_button_y
#define KENNEY_STEAM_BUTTON_Y                 KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_Y_CP)
#define KENNEY_STEAM_BUTTON_BACK_CP           0xE026  // steam_button_back_icon
#define KENNEY_STEAM_BUTTON_BACK              KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_BACK_CP)
#define KENNEY_STEAM_BUTTON_START_CP          0xE034  // steam_button_start_icon
#define KENNEY_STEAM_BUTTON_START             KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_START_CP)
#define KENNEY_STEAM_BUTTON_L4_CP             0xE004  // controller_button_l4 (back grip paddle)
#define KENNEY_STEAM_BUTTON_L4                KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_L4_CP)
#define KENNEY_STEAM_BUTTON_L5_CP             0xE006  // controller_button_l5 (back grip paddle)
#define KENNEY_STEAM_BUTTON_L5                KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_L5_CP)
#define KENNEY_STEAM_BUTTON_R4_CP             0xE010  // controller_button_r4 (back grip paddle)
#define KENNEY_STEAM_BUTTON_R4                KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_R4_CP)
#define KENNEY_STEAM_BUTTON_R5_CP             0xE012  // controller_button_r5 (back grip paddle)
#define KENNEY_STEAM_BUTTON_R5                KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_R5_CP)
#define KENNEY_STEAM_BUTTON_QUICKACCESS_CP    0xE00A  // controller_button_quickaccess
#define KENNEY_STEAM_BUTTON_QUICKACCESS       KENNEY_ICON_STR(KENNEY_STEAM_BUTTON_QUICKACCESS_CP)
#define KENNEY_STEAM_CONTROLLER_ICON_CP       0xE016  // controller_icon (Steam logo)
#define KENNEY_STEAM_CONTROLLER_ICON          KENNEY_ICON_STR(KENNEY_STEAM_CONTROLLER_ICON_CP)
#define KENNEY_STEAM_LB_CP                    0xE049  // steam_lb
#define KENNEY_STEAM_LB                       KENNEY_ICON_STR(KENNEY_STEAM_LB_CP)
#define KENNEY_STEAM_RB_CP                    0xE055  // steam_rb
#define KENNEY_STEAM_RB                       KENNEY_ICON_STR(KENNEY_STEAM_RB_CP)
#define KENNEY_STEAM_LT_CP                    0xE04D  // steam_lt
#define KENNEY_STEAM_LT                       KENNEY_ICON_STR(KENNEY_STEAM_LT_CP)
#define KENNEY_STEAM_RT_CP                    0xE059  // steam_rt
#define KENNEY_STEAM_RT                       KENNEY_ICON_STR(KENNEY_STEAM_RT_CP)
#define KENNEY_STEAM_DPAD_UP_CP               0xE045  // steam_dpad_up
#define KENNEY_STEAM_DPAD_UP                  KENNEY_ICON_STR(KENNEY_STEAM_DPAD_UP_CP)
#define KENNEY_STEAM_DPAD_DOWN_CP             0xE03C  // steam_dpad_down
#define KENNEY_STEAM_DPAD_DOWN                KENNEY_ICON_STR(KENNEY_STEAM_DPAD_DOWN_CP)
#define KENNEY_STEAM_DPAD_LEFT_CP             0xE040  // steam_dpad_left
#define KENNEY_STEAM_DPAD_LEFT                KENNEY_ICON_STR(KENNEY_STEAM_DPAD_LEFT_CP)
#define KENNEY_STEAM_DPAD_RIGHT_CP            0xE043  // steam_dpad_right
#define KENNEY_STEAM_DPAD_RIGHT               KENNEY_ICON_STR(KENNEY_STEAM_DPAD_RIGHT_CP)
#define KENNEY_STEAM_STICK_HORIZONTAL_CP      0xE05D  // steam_stick_horizontal
#define KENNEY_STEAM_STICK_HORIZONTAL         KENNEY_ICON_STR(KENNEY_STEAM_STICK_HORIZONTAL_CP)
#define KENNEY_STEAM_STICK_VERTICAL_CP        0xE063  // steam_stick_vertical
#define KENNEY_STEAM_STICK_VERTICAL           KENNEY_ICON_STR(KENNEY_STEAM_STICK_VERTICAL_CP)
#define KENNEY_STEAM_STICK_L_PRESS_CP         0xE05E  // steam_stick_l_press
#define KENNEY_STEAM_STICK_L_PRESS            KENNEY_ICON_STR(KENNEY_STEAM_STICK_L_PRESS_CP)
#define KENNEY_STEAM_PAD_CP                   0xE04F  // steam_pad (right trackpad, swipe)
#define KENNEY_STEAM_PAD                      KENNEY_ICON_STR(KENNEY_STEAM_PAD_CP)
#define KENNEY_STEAM_PAD_CENTER_CP            0xE050  // steam_pad_center (right trackpad, click)
#define KENNEY_STEAM_PAD_CENTER               KENNEY_ICON_STR(KENNEY_STEAM_PAD_CENTER_CP)
