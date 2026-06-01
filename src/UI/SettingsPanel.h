#pragma once

#include <SDL3/SDL.h>
#include "imgui.h"
#include "Preferences/Preferences.h"

/// Renders the Settings section content without any Begin/End wrapper.
/// The caller is responsible for placing this inside a suitable child window.
///
/// Parameters that are modified by user interaction (scale values, vsync,
/// battery LED toggle) are passed by reference so the Application can react
/// immediately (e.g. rebuild the font atlas or toggle VSync on the renderer).
void DrawSettingsContent(float&              user_ui_scale,
                         float&              user_font_scale,
                         bool&               scale_with_window,
                         SDL_Window*         window,
                         int                 initial_width,
                         int                 initial_height,
                         PreferencesManager& prefs,
                         bool&               vsync,
                         int&                framerate_limit,
                         SDL_Renderer*       renderer,
                         const ImGuiIO&      io,
                         bool&               enable_battery_led,
                         bool&               disable_gamepad_nav,
                         bool&               disable_keyboard_nav);
