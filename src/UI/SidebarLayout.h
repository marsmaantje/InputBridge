#pragma once

#include <SDL3/SDL.h>
#include "Preferences/Preferences.h"

class DeviceManager;
class InputMapper;
class OutputMapper;

/// Bundles all mutable Application state that the sidebar layout reads or
/// writes.  Using an aggregate struct keeps the call site readable and makes
/// it straightforward to add or remove fields without touching every caller.
/// References are used for values the sidebar needs to mutate; the window and
/// renderer pointers are non-owning raw pointers (lifetime is the Application).
struct SidebarContext {
    DeviceManager&      deviceManager;
    PreferencesManager& prefs;
    InputMapper&        inputMapper;
    OutputMapper&       outputMapper;

    // Render settings
    bool&         vsync;
    int&          framerate_limit;
    SDL_Renderer* renderer;

    // Network update rate
    int&  server_update_rate;
    bool& server_dynamic_rate;
    float current_messages_per_second; // read-only snapshot, passed by value

    // UI scale (may be modified by the Settings panel)
    float&        user_ui_scale;
    float&        user_font_scale;
    bool&         scale_with_window;

    // Window geometry (initial size is constant; pointer for SDL calls)
    SDL_Window*   window;
    int           initial_width;
    int           initial_height;

    // Set to true by the Exit button to signal the main loop to stop.
    bool& done;

    // Generic visualizer option persisted in Settings
    bool& show_named_inputs;
};

/// Renders the full sidebar navigation panel and the right-hand content area.
/// Must be called once per ImGui frame, after ImGui::NewFrame() and before
/// ImGui::Render().
void DrawSidebarLayout(SidebarContext& ctx);
