#pragma once
// IconsFontAwesome6.h
// Minimal subset of Font Awesome 6 Free (Solid) codepoints used by InputBridge.
//
// Full header (all 1400+ icons): https://github.com/juliettef/IconFontCppHeaders
// Font file download:            https://fontawesome.com/download  (Free → "For Desktop")
//   Place fa-solid-900.ttf in the fonts/ folder next to the executable.
//
// Range used for the ImGui font atlas merge:

#define ICON_MIN_FA  0xf000
#define ICON_MAX_FA  0xf8ff

// Font file name expected in the fonts/ directory
#define FONT_ICON_FILE_NAME_FAS "fa-solid-900.ttf"

// ── Icons used in the sidebar ────────────────────────────────────────────────
// Raw UTF-8 byte sequences (plain const char*) — avoids the C++20 char8_t
// incompatibility that u8"" literals introduced.
//
// Each codepoint is in the Private Use Area (U+F000–U+F8FF), encoded as
// 3-byte UTF-8:  U+FXYZ → 0xEF  0x8X+  0xYZ+  (see table below)
//
//  U+F11B  →  \xEF\x84\x9B   gamepad
//  U+F1DE  →  \xEF\x87\x9E   sliders
//  U+F0E7  →  \xEF\x83\xA7   bolt / lightning
//  U+F1EB  →  \xEF\x87\xAB   wifi
//  U+F1C9  →  \xEF\x87\x89   file-code
//  U+F013  →  \xEF\x80\x93   gear
//  U+F05A  →  \xEF\x81\x9A   info-circle
//  U+F011  →  \xEF\x80\x91   power-off
//  U+F188  →  \xEF\x86\x88   bug  (Debug Log)

#define ICON_FA_GAMEPAD       "\xEF\x84\x9B"   // Devices
#define ICON_FA_SLIDERS       "\xEF\x87\x9E"   // Input Mapper / Analog axis
#define ICON_FA_WAVE_SQUARE   "\xEF\xA0\xBE"   // Digital button (square wave)
#define ICON_FA_BOLT          "\xEF\x83\xA7"   // Output Mapper
#define ICON_FA_WIFI          "\xEF\x87\xAB"   // Network
#define ICON_FA_FILE_CODE     "\xEF\x87\x89"   // Protocol Editor
#define ICON_FA_GEAR          "\xEF\x80\x93"   // UI Settings
#define ICON_FA_INFO_CIRCLE   "\xEF\x81\x9A"   // About
#define ICON_FA_POWER_OFF     "\xEF\x80\x91"   // Exit
#define ICON_FA_BUG           "\xEF\x86\x88"   // Debug Log