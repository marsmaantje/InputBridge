#pragma once

#include <SDL3/SDL.h>
#include "Preferences/Preferences.h"
#include <string>

/// Recomputes and applies DPI / user-scale to the active ImGui style.
///
/// Resets ImGuiStyle to defaults before scaling, then re-applies the current
/// theme so values are never compounded across multiple calls.  Must be called
/// whenever the display scale or the user's UI-Scale preference changes.
void UpdateUIScale(SDL_Window*         window,
                   float&              user_ui_scale,
                   float&              user_font_scale,
                   bool                scale_with_window,
                   int                 initial_width,
                   PreferencesManager& prefs);

/// Rebuilds the ImGui font atlas from the current ThemeManager settings.
///
/// Must be called BEFORE ImGui_ImplSDLRenderer3_NewFrame().  Falls back to
/// the built-in default font when the theme font file cannot be loaded.
/// Font Awesome 6 Free (Solid) is merged as a second pass when
/// fonts/fa-solid-900.ttf is present next to the executable.
///
/// The SDL3 renderer backend manages the GPU texture automatically; calling
/// this function is sufficient — no manual Create/Destroy is required.
void RebuildFontAtlas();

/// Full path to the fonts/ folder next to the executable
/// (SDL_GetBasePath() + "fonts/"). Used both for loading the Font Awesome
/// icon font and for the Settings page's "open folder" button.
std::string GetFontsDir();
