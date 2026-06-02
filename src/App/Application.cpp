#include "Application.h"
#include "AppLog.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "Devices/DeviceManager.h"
#include "Devices/SensorReader.h"
#include "Devices/VirtualDeviceManager.h"
#include "Mappers/InputMapper.h"
#include "Mappers/OutputMapper.h"
#include "Network/OSCServer.h"
#include "Network/WebSocketServer.h"
#include "Preferences/Preferences.h"
#include "Protocols/OSCProtocol.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "UI/FontManager.h"
#include "UI/SidebarLayout.h"
#include "UI/ThemeManager.h"
#include "Utils/XdgDirs.h"

#if ENABLE_WEBSOCKETS
#include "Protocols/WebSocketProtocol.h"
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

// ── RegisterProtocols ─────────────────────────────────────────────────────────

void Application::RegisterProtocols()
{
    ProtocolManager::GetInstance().RegisterProtocol(
        std::make_shared<OSCProtocol>());
#if ENABLE_WEBSOCKETS
    ProtocolManager::GetInstance().RegisterProtocol(
        std::make_shared<WebSocketProtocol>());
#endif
}

// ── SetSDLHints ───────────────────────────────────────────────────────────────

void Application::SetSDLHints()
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM,            "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM_HOME_LED,   "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI,                  "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS,        "1");

    // ── Nintendo Switch / Joy-Con ────────────────────────────────────────────
    // Enable the HIDAPI driver so gyro, accel, and rumble are accessible on
    // Switch Pro Controllers and Joy-Cons.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");

    // When a Left and Right Joy-Con are both connected, merge them into a
    // single virtual gamepad.  In merged mode SDL exposes SDL_SENSOR_GYRO_L
    // and SDL_SENSOR_GYRO_R (one per physical controller) rather than two
    // separate devices each with only SDL_SENSOR_GYRO.  The existing GyroL /
    // GyroR and AccelL / AccelR sensor channels then address each Joy-Con's
    // IMU independently.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "1");

    // ── PlayStation ──────────────────────────────────────────────────────────
    // Enable HIDAPI for DualShock 4 and DualSense so touchpad, gyro, and accel
    // are available even when connected over USB without Steam Input.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
}

// ── CreateWindow ─────────────────────────────────────────────────────────────

bool Application::CreateAppWindow()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
        std::printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return false;
    }

    const Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    // Get version number from top of CMakeLists
    m_window = SDL_CreateWindow("InputBridge v" INPUTBRIDGE_VERSION,
                                k_InitialWidth, k_InitialHeight, window_flags);
    if (!m_window) {
        std::printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        std::printf("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        return false;
    }

    return true;
}

// ── SetupImGui ────────────────────────────────────────────────────────────────

void Application::SetupImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // imgui.ini stores UI layout state — it is written at runtime so it must
    // live in the writable XDG config directory, not next to the executable
    // (which is read-only inside an AppImage squashfs mount).
    m_iniFilename = XdgDirs::configDir() + "imgui.ini";
    io.IniFilename = m_iniFilename.c_str();

    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer);
    ImGui_ImplSDLRenderer3_Init(m_renderer);
}

// ── InitialDeviceScan ────────────────────────────────────────────────────────

void Application::InitialDeviceScan()
{
    DeviceManager& dm = DeviceManager::GetInstance();
    int            count = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
    if (joysticks) {
        for (int i = 0; i < count; ++i)
            dm.HandleDeviceAdded(joysticks[i]);
        SDL_free(joysticks);
    }
}

// ── MigrateUserData ───────────────────────────────────────────────────────────
//
// Versions of InputBridge prior to the XDG path migration stored all user data
// under the SDL pref path, which on Linux resolves to the double-nested:
//
//   ~/.local/share/InputBridge/InputBridge/
//
// After the XDG migration the layout is:
//
//   Config  →  ~/.config/InputBridge/        (visualizer_prefs.toml, imgui.ini)
//   Data    →  ~/.local/share/InputBridge/   (mappings/, protocols/)
//
// This method runs once on startup before any subsystem reads a file.  It
// copies files from the old location to the new one, skipping files that
// already exist at the destination so a partial or repeated migration is safe.
// The old directory is intentionally left intact so a downgrade still works.
//
// Only runs on Linux/FreeBSD — on Windows and macOS SDL_GetPrefPath was never
// changed, so there is nothing to migrate on those platforms.

void Application::MigrateUserData()
{
#if !defined(__linux__) && !defined(__FreeBSD__)
    return;
#else
    namespace fs = std::filesystem;

    // Reconstruct the old SDL pref path from $HOME rather than calling
    // SDL_GetPrefPath, which would create the directory if it is absent.
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') return;

    const fs::path oldRoot = fs::path(home) / ".local/share/InputBridge/InputBridge";
    if (!fs::exists(oldRoot)) return; // Fresh install — nothing to do.

    const fs::path newConfig = XdgDirs::configDir(); // ~/.config/InputBridge/
    const fs::path newData   = XdgDirs::dataDir();   // ~/.local/share/InputBridge/

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Copy a single file src → dst.  A missing source or pre-existing
    // destination are both silent no-ops (returns false, not an error).
    auto migrateFile = [](const fs::path& src, const fs::path& dst) -> bool {
        if (!fs::exists(src) || fs::exists(dst)) return false;
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        if (ec) {
            SDL_Log("[Migration] Could not create directory %s: %s",
                    dst.parent_path().string().c_str(), ec.message().c_str());
            return false;
        }
        fs::copy_file(src, dst, fs::copy_options::skip_existing, ec);
        if (ec) {
            SDL_Log("[Migration] Could not copy %s → %s: %s",
                    src.string().c_str(), dst.string().c_str(), ec.message().c_str());
            return false;
        }
        SDL_Log("[Migration] %s → %s", src.string().c_str(), dst.string().c_str());
        return true;
    };

    // Recursively mirror srcDir → dstDir, skipping pre-existing files.
    // Returns the count of files actually copied.
    auto migrateDir = [&migrateFile](const fs::path& srcDir,
                                     const fs::path& dstDir) -> int {
        if (!fs::exists(srcDir)) return 0;
        int count = 0;
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(srcDir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (migrateFile(entry.path(), dstDir / fs::relative(entry.path(), srcDir)))
                ++count;
        }
        return count;
    };

    // ── Config files (root of old SDL pref dir) ───────────────────────────────
    int total = 0;
    total += migrateFile(oldRoot / "visualizer_prefs.toml", newConfig / "visualizer_prefs.toml") ? 1 : 0;
    total += migrateFile(oldRoot / "imgui.ini",             newConfig / "imgui.ini")             ? 1 : 0;

    // ── Data directories ──────────────────────────────────────────────────────
    total += migrateDir(oldRoot / "mappings",  newData / "mappings");
    total += migrateDir(oldRoot / "protocols", newData / "protocols");

    if (total > 0)
        SDL_Log("[Migration] Migrated %d file(s) from legacy path %s",
                total, oldRoot.string().c_str());
    else
        SDL_Log("[Migration] Legacy path %s exists but all files already migrated.",
                oldRoot.string().c_str());
#endif
}

// ── RestorePreferences ────────────────────────────────────────────────────────

void Application::RestorePreferences()
{
    m_prefs.Load();

    m_uiScale         = m_prefs.GetFloat(PrefKeys::UIScale,           1.3f);
    m_fontScale       = m_prefs.GetFloat(PrefKeys::FontScale,         1.0f);
    m_scaleWithWindow = m_prefs.GetBool(PrefKeys::ScaleWithWindow,    false);
    m_serverUpdateRate  = m_prefs.GetInt(PrefKeys::NetworkSection, PrefKeys::UpdateRate, 60);
    m_serverDynamicRate = m_prefs.GetBool(PrefKeys::NetworkSection, PrefKeys::DynamicRate, false);

    // Scan and restore the active theme BEFORE UpdateUIScale so the theme's
    // style values (rounded corners, etc.) are applied before scaling.
    const char* base_path = SDL_GetBasePath();
    const std::string scanBase = base_path ? std::string(base_path) : ".";
    ThemeManager::GetInstance().ScanThemesDirectory(scanBase);
    ThemeManager::GetInstance().LoadFromPreferences(m_prefs);

    UpdateUIScale(m_window, m_uiScale, m_fontScale,
                  m_scaleWithWindow, k_InitialWidth, m_prefs);

    if (ThemeManager::GetInstance().HasPendingFontChange())
        RebuildFontAtlas();

    // Initialise mappers and network servers.
    // ProtocolRegistry::LoadAll() MUST run before InputMapper::LoadConfig() so
    // that the definitions list (used by GetActiveOutputDefinition()) is
    // populated when ActivateProfile() fires inside LoadConfig.  Loading
    // profiles first left m_definitions empty, causing FindById() to return
    // nullptr, which hid the Digital Output Channel section on every startup.
    OutputMapper::Init(DeviceManager::GetInstance());
    ProtocolRegistry::GetInstance().LoadAll();
    InputMapper::Init(DeviceManager::GetInstance());
    InputMapper::GetInstance().LoadConfig(m_prefs);

    // SetOutputMapper MUST be called before LoadConfig: LoadConfig will call
    // Start() if the server was previously enabled, and any haptic messages
    // arriving between Start() and a late SetOutputMapper() would be silently
    // dropped because m_OutputMapper is still null.
    OutputMapper& om = OutputMapper::GetInstance();
    WebSocketServer::GetInstance().SetOutputMapper(&om);
    OSCServer::GetInstance().SetOutputMapper(&om);

    OSCServer::GetInstance().LoadConfig(m_prefs);
    WebSocketServer::GetInstance().LoadConfig(m_prefs);
}

// ── Init ─────────────────────────────────────────────────────────────────────

bool Application::Init()
{
    // Install the log sink before anything else so no early SDL_Log messages
    // are missed.
    AppLog::Install();

    RegisterProtocols();
    SetSDLHints();

    if (!CreateAppWindow())
        return false;

    SetupImGui();
    InitialDeviceScan();
    MigrateUserData();
    RestorePreferences();

    SDL_SetRenderVSync(m_renderer, 1);
    m_lastMpsUpdate = SDL_GetTicks();

    return true;
}

// ── ProcessEvents ─────────────────────────────────────────────────────────────

void Application::ProcessEvents()
{
    DeviceManager& dm = DeviceManager::GetInstance();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT)
            m_running = false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
            && event.window.windowID == SDL_GetWindowID(m_window))
            m_running = false;

        if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED
            && event.window.windowID == SDL_GetWindowID(m_window)) {
            UpdateUIScale(m_window, m_uiScale, m_fontScale,
                          m_scaleWithWindow, k_InitialWidth, m_prefs);
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED
            && m_scaleWithWindow
            && event.window.windowID == SDL_GetWindowID(m_window)) {
            UpdateUIScale(m_window, m_uiScale, m_fontScale,
                          m_scaleWithWindow, k_InitialWidth, m_prefs);
        }

        if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
            bool already_connected = false;
            for (const auto& dev : dm.GetDevices()) {
                if (dev.instance_id == event.jdevice.which) {
                    already_connected = true;
                    break;
                }
            }
            if (!already_connected)
                dm.HandleDeviceAdded(event.jdevice.which);
        }
        if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
            dm.HandleDeviceRemoved(event.jdevice.which);
            m_prefs.ClearAppliedPreference(event.jdevice.which);
        }

        // Re-enable IMU sensors on all gamepads whenever the window regains
        // focus.  Steam Input (and other overlays) can silently reset
        // SDL_SetGamepadSensorEnabled to false when they take control of the
        // device — even without triggering a remove/add cycle.  Re-enabling
        // here ensures gyro and accel readings resume as soon as the user
        // returns to InputBridge (e.g. after dismissing the Steam overlay).
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            for (auto& dev : dm.GetDevices()) {
                if (dev.gamepad)
                    SensorReader::EnableAll(dev.gamepad);
            }
        }
    }
}

// ── UpdateLogic ───────────────────────────────────────────────────────────────

void Application::UpdateLogic(Uint64 frame_start_time)
{
    OutputMapper& om = OutputMapper::GetInstance();
    InputMapper&  im = InputMapper::GetInstance();

    // Haptics always run at full frame rate for minimum latency.
    om.Update();

    // Refresh battery percent / charging state on a timer (every 5 s active,
    // 30 s minimized).  Must run every frame so the timer fires on schedule;
    // the method itself gates SDL calls to the configured interval.
    const bool isMinimized = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
    DeviceManager::GetInstance().Update(isMinimized);

    // Per-server inactivity checks: fire StopAllHapticEffects once when
    // a connected client stops sending data for X seconds.
    OSCServer::GetInstance().CheckInactivity();
    WebSocketServer::GetInstance().CheckInactivity();

    // Cross-server check: if at least one server is running but neither has
    // any clients at all, stop all haptic effects on the transition edge.
    {
        const bool oscRunning = OSCServer::GetInstance().IsRunning();
        const bool wsRunning  = WebSocketServer::GetInstance().IsRunning();
        const bool anyServerRunning = oscRunning || wsRunning;

        const bool oscHasClients = oscRunning && OSCServer::GetInstance().HasClients();
        const bool wsHasClients  = wsRunning  && (WebSocketServer::GetInstance().GetClientCount() > 0);
        const bool anyClients    = oscHasClients || wsHasClients;

        const bool noClientsNow = anyServerRunning && !anyClients;
        if (noClientsNow && !m_hadNoClients) {
            om.StopAllHapticEffects();
        }
        m_hadNoClients = noClientsNow;
    }

    // Push virtual device state so InputMapper reads current values.
    VirtualDeviceManager::GetInstance().PushAllStates();

    // Decide whether to poll and dispatch input this frame.
    bool check_input_update = m_serverDynamicRate;
    if (!check_input_update && m_serverUpdateRate > 0) {
        const Uint64 interval = 1000 / static_cast<Uint64>(m_serverUpdateRate);
        check_input_update = (frame_start_time - m_lastServerUpdate) >= interval;
    }

    if (check_input_update && im.Update(m_serverDynamicRate)) {
        m_lastServerUpdate = frame_start_time;
        ++m_msgSentCounter;
    }

    // Update messages-per-second counter once per second.
    if (frame_start_time - m_lastMpsUpdate >= 1000) {
        const float elapsed = static_cast<float>(frame_start_time - m_lastMpsUpdate) / 1000.0f;
        m_currentMPS        = static_cast<float>(m_msgSentCounter) / elapsed;
        m_msgSentCounter    = 0;
        m_lastMpsUpdate     = frame_start_time;
    }
}

// ── RenderFrame ───────────────────────────────────────────────────────────────

void Application::RenderFrame(Uint64 frame_start_time)
{
    // Rebuild font atlas if a theme change requested a different font.
    // Must happen before ImGui_ImplSDLRenderer3_NewFrame().
    if (ThemeManager::GetInstance().HasPendingFontChange())
        RebuildFontAtlas();

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    SidebarContext ctx{
        DeviceManager::GetInstance(),
        m_prefs,
        InputMapper::GetInstance(),
        OutputMapper::GetInstance(),
        m_vsync,
        m_framerateLimit,
        m_renderer,
        m_serverUpdateRate,
        m_serverDynamicRate,
        m_currentMPS,
        m_uiScale,
        m_fontScale,
        m_scaleWithWindow,
        m_window,
        k_InitialWidth,
        k_InitialHeight,
        m_running
    };
    DrawSidebarLayout(ctx);

    // ── SDL render ────────────────────────────────────────────────────────
    ImGui::Render();

    int w = 0, h = 0, bbw = 0, bbh = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    SDL_GetWindowSizeInPixels(m_window, &bbw, &bbh);
    const float scale_x = (w > 0) ? (static_cast<float>(bbw) / w) : 1.0f;
    const float scale_y = (h > 0) ? (static_cast<float>(bbh) / h) : 1.0f;
    SDL_SetRenderScale(m_renderer, scale_x, scale_y);

    SDL_SetRenderDrawColor(m_renderer, 30, 30, 30, 255);
    SDL_RenderClear(m_renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    SDL_RenderPresent(m_renderer);

    // Software frame cap when VSync is disabled.
    if (!m_vsync && m_framerateLimit > 0) {
        const Uint64 frame_end      = SDL_GetTicks();
        const Uint64 frame_duration = frame_end - frame_start_time;
        const Uint64 target         = 1000 / static_cast<Uint64>(m_framerateLimit);
        if (frame_duration < target)
            SDL_Delay(static_cast<Uint32>(target - frame_duration));
    }
}

// ── Run ───────────────────────────────────────────────────────────────────────

void Application::Run()
{
    while (m_running) {
        const Uint64 frame_start = SDL_GetTicks();
        ProcessEvents();
        UpdateLogic(frame_start);
        RenderFrame(frame_start);
    }
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void Application::Shutdown()
{
    // ── Release all protocol instances first ──────────────────────────────────
    // ProtocolManager is a static singleton constructed before OSCServer, so it
    // would be destroyed AFTER OSCServer during normal static teardown.
    // Protocol destructors (e.g. OSCProtocol::~OSCProtocol) call back into
    // OSCServer::GetInstance(), which would be a use-after-destruction crash.
    // Clearing the protocol map now — while all singletons are still alive —
    // prevents that entire class of ordering bugs.
    ProtocolManager::GetInstance().Clear();

    // ── Stop network servers BEFORE destroying mappers ────────────────────────
    // Both OSCServer::Stop() and WebSocketServer::Stop() call
    // m_OutputMapper->StopAllHapticEffects() synchronously via their stored raw
    // pointer.  If OutputMapper::Shutdown() runs first it frees the OutputMapper
    // object, turning those calls into use-after-free crashes (segfault at
    // OutputMapper.cpp StopAllHapticEffects).  Stopping the servers here —
    // while OutputMapper is still alive — prevents that entirely.
    OSCServer::GetInstance().SaveConfig(m_prefs);
    WebSocketServer::GetInstance().SaveConfig(m_prefs);
    OSCServer::GetInstance().Stop();
    WebSocketServer::GetInstance().Stop();

    // Wait for the OSC liblo cleanup thread and the uWS event-loop thread to
    // fully exit before OutputMapper is destroyed.  Both Stop() calls above
    // return immediately and move their blocking teardown to background threads;
    // those threads hold raw OutputMapper* pointers and can still invoke
    // callbacks (e.g. StopAllHapticEffects) until they have fully terminated.
    // Joining here guarantees no callback can fire after Shutdown() frees the
    // object, eliminating the use-after-free that causes the segfault.
    OSCServer::GetInstance().WaitStopped();
    WebSocketServer::GetInstance().WaitStopped();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    InputMapper& im = InputMapper::GetInstance();
    im.SaveConfig(m_prefs);
    InputMapper::Shutdown();
    OutputMapper::Shutdown();

    // Close haptic and joystick devices before SDL_Quit().  DeviceManager is a
    // static singleton; its destructor (which calls CloseAllDevices) runs after
    // main() returns and therefore after SDL_Quit() — too late.  Calling it
    // explicitly here keeps all SDL API calls within SDL's lifetime.
    DeviceManager::GetInstance().CloseAllDevices();

    ProtocolRegistry::GetInstance().SaveAll();

    m_prefs.SetInt(PrefKeys::NetworkSection,  PrefKeys::UpdateRate,  m_serverUpdateRate);
    m_prefs.SetBool(PrefKeys::NetworkSection, PrefKeys::DynamicRate, m_serverDynamicRate);
    m_prefs.Save();

    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}