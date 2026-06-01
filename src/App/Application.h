#pragma once

#include <SDL3/SDL.h>
#include <string>
#include "Preferences/Preferences.h"

/// Top-level application object.  Owns the SDL window, renderer, and ImGui
/// context.  All other subsystems are either singletons (DeviceManager,
/// InputMapper, etc.) or managed here as plain members.
///
/// Typical usage:
/// @code
///   Application app;
///   if (!app.Init())
///       return 1;
///   app.Run();
///   app.Shutdown();
/// @endcode
class Application {
public:
    Application() = default;
    ~Application() = default;

    // Non-copyable, non-movable: owns SDL and ImGui resources.
    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&)                 = delete;
    Application& operator=(Application&&)      = delete;

    /// Registers protocols, initialises SDL, creates the window + renderer,
    /// sets up the ImGui context, and restores saved preferences.
    /// Returns false if any critical step fails; the caller should exit.
    [[nodiscard]] bool Init();

    /// Runs the main loop until the user closes the window or clicks Exit.
    void Run();

    /// Releases all resources acquired by Init() in reverse order.
    void Shutdown();

private:
    // ── Initialisation helpers (called once from Init) ────────────────────
    void RegisterProtocols();
    void SetSDLHints();
    [[nodiscard]] bool CreateAppWindow();
    void SetupImGui();
    void InitialDeviceScan();
    void MigrateUserData();   // one-time migration from pre-XDG SDL pref paths
    void RestorePreferences();

    // ── Per-frame helpers (called from Run) ───────────────────────────────
    void ProcessEvents();
    void UpdateLogic(Uint64 frame_start_time);
    void RenderFrame(Uint64 frame_start_time);

    // ── SDL / ImGui objects ───────────────────────────────────────────────
    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    std::string   m_iniFilename;   // storage for ImGuiIO::IniFilename

    // ── Preferences ───────────────────────────────────────────────────────
    PreferencesManager m_prefs;

    // ── UI scale (persisted) ──────────────────────────────────────────────
    float m_uiScale         = 1.3f;
    float m_fontScale       = 1.0f;
    bool  m_scaleWithWindow = false;

    // ── Render settings ───────────────────────────────────────────────────
    bool m_running        = true;
    bool m_vsync          = true;
    int  m_framerateLimit = 60;

    // Tracks the previous no-clients state so StopAllHapticEffects fires
    // exactly once on the transition from clients-present to no-clients.
    bool m_hadNoClients = false;

    // ── Network update-rate tracking ──────────────────────────────────────
    int    m_serverUpdateRate  = 60;
    bool   m_serverDynamicRate = false;
    Uint64 m_lastServerUpdate  = 0;
    int    m_msgSentCounter    = 0;
    float  m_currentMPS        = 0.0f;
    Uint64 m_lastMpsUpdate     = 0;

    // ── Window geometry ───────────────────────────────────────────────────
    static constexpr int k_InitialWidth  = 1280;
    static constexpr int k_InitialHeight = 720;
};