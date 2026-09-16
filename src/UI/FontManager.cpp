#include "App/Log.h"
#include "FontManager.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "UI/DeviceIconProvider.h"
#include "UI/IconsFontAwesome6.h"
#include "UI/KenneyIcons.h"
#include "UI/ThemeManager.h"

#include <string>

static constexpr const char* kTag = "FontManager";

void UpdateUIScale(SDL_Window*         window,
                   float&              user_ui_scale,
                   float&              user_font_scale,
                   bool                scale_with_window,
                   int                 initial_width,
                   PreferencesManager& prefs)
{
    float scale   = SDL_GetWindowDisplayScale(window);
    float density = SDL_GetWindowPixelDensity(window);
    if (density <= 0.0f) density = 1.0f;
    if (scale   <= 0.0f) scale   = 1.0f;
    float ui_scale = scale / density;

    if (scale_with_window) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        user_ui_scale = static_cast<float>(w) / static_cast<float>(initial_width);
        prefs.SetFloat("UIScale", user_ui_scale);
    }
    ui_scale *= user_ui_scale;

    // Reset to a clean base so scale values are never compounded.
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ThemeManager::GetInstance().ApplyBaseColors();

    // Re-apply the active theme on top of the fresh default style.
    ThemeManager::GetInstance().Reapply();

    style.ScaleAllSizes(ui_scale);
    if (style.WindowBorderHoverPadding <= 0.0f)
        style.WindowBorderHoverPadding = 1.0f;

    ImGui::GetStyle().FontScaleMain = ui_scale * user_font_scale;
}

std::string GetFontsDir() {
    const char* base = SDL_GetBasePath();
    return (base ? std::string(base) : std::string(".")) + "fonts/";
}

void RebuildFontAtlas()
{
    ImGuiIO&      io    = ImGui::GetIO();
    ThemeManager& theme = ThemeManager::GetInstance();

    io.Fonts->Clear();
    
    const std::string& fontPath = theme.GetResolvedFontPath();
    const float        themeFontSize = theme.GetFontSize(); // Renamed to avoid confusion with icon font size parameter
    
    ImFont* base_font = nullptr;
    bool loaded_theme_font = false;
    if (!fontPath.empty()) {
        base_font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), themeFontSize);
        loaded_theme_font = (base_font != nullptr);
        if (!loaded_theme_font)
            LOG_WARN(kTag, "Font: Failed to load '%s' - falling back to default.",
                    fontPath.c_str());
    }
    if (!loaded_theme_font)
        io.Fonts->AddFontDefault();

    // Merge Font Awesome 6 Free (Solid) icons.
    // The TTF must be at fonts/fa-solid-900.ttf next to the executable.
    // If absent the app still works; buttons show the fallback text instead.
    {
        std::string iconFontPath = GetFontsDir() + FONT_ICON_FILE_NAME_FAS;

        // Icon glyphs are rendered at the same pixel size as the text font so
        // they sit on the baseline naturally.  GlyphMinAdvanceX makes them
        // fixed-width so sidebar columns stay aligned.
        static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        ImFontConfig cfg;
        cfg.MergeMode        = true;
        cfg.PixelSnapH       = true;

        // In ImGui 1.92, specifying custom advances/offsets requires an explicit 
        // reference size. If the base font is implicit (scalable), we must merge 
        // with 0.0f and cannot use these fields.
        float merge_size = 0.0f;
        if (loaded_theme_font) {
            merge_size = themeFontSize;
            cfg.GlyphMinAdvanceX = merge_size;
            cfg.GlyphMaxAdvanceX = merge_size;
            cfg.GlyphOffset      = ImVec2(0.0f, 1.0f); // nudge 1 px down to align baseline
        }

        ImFont* icons = io.Fonts->AddFontFromFileTTF(
            iconFontPath.c_str(), merge_size, &cfg, icon_ranges);

        if (!icons)
            LOG_WARN(kTag, "Font: FA6 not found at '%s' - icon glyphs will be missing.",
                    iconFontPath.c_str());
    }

    // -- Kenney Input Prompt fonts -----------------------------------------
    // Each Kenney font occupies the same Private Use Area block (U+E000+),
    // so they cannot be merged into the base font.  Instead each is loaded as
    // a separate ImFont* stored in KenneyFonts.  DeviceIconProvider pushes the
    // appropriate font with ImGui::PushFont() when it renders a device icon.
    //
    // Missing font files are silently skipped; DeviceIconProvider returns an
    // invalid DeviceIcon in that case and the caller simply omits the icon.
    {
        KenneyFonts& kf = KenneyFonts::Get();
        kf.Reset(); // invalidate previous atlas pointers before rebuilding

        // Glyph range covering U+E000–U+F8FF (entire Kenney PUA block).
        static const ImWchar kenney_ranges[] = { 0xE000, 0xF8FF, 0 };

        // Kenney pictogram glyphs only occupy ~25-50% of their em square, so
        // DevicePanel inflates render_sz by 1/fill to make the visible pixels
        // match the text height.  The worst-case fill is ~0.25 (Steam Deck),
        // meaning render_sz can reach themeFontSize / 0.25 = 4× themeFontSize.
        // The atlas bake size must be >= the render_sz to avoid upscaling
        // artefacts, so we bake at 4× to cover every font in the worst case.
        // DevicePanel calls GetFontBaked(FontSizeBase) to retrieve glyph
        // metrics; the bake at 4× FontSizeBase is what gets returned and used
        // for the Y0/Y1 fill calculation.
        const float kenney_size = (themeFontSize > 0.0f ? themeFontSize : 16.0f) * 4.0f;

        const std::string fontsDir = GetFontsDir();

        // Helper: load one Kenney TTF and return the ImFont* (or nullptr).
        auto LoadKenney = [&](const char* subDir, const char* file) -> ImFont*
        {
            std::string path = fontsDir + subDir + file;
            ImFontConfig cfg;
            cfg.PixelSnapH = true;
            ImFont* f = io.Fonts->AddFontFromFileTTF(
                path.c_str(), kenney_size, &cfg, kenney_ranges);
            if (!f)
                LOG_WARN(kTag,
                    "Kenney font not found at '%s' - device icons may be missing.",
                    path.c_str());
            return f;
        };

        kf.xbox             = LoadKenney(KENNEY_DIR_XBOX,           KENNEY_FILE_XBOX);
        kf.playstation      = LoadKenney(KENNEY_DIR_PLAYSTATION,    KENNEY_FILE_PLAYSTATION);
        kf.nintendoSwitch   = LoadKenney(KENNEY_DIR_SWITCH,         KENNEY_FILE_SWITCH);
        kf.nintendoWii      = LoadKenney(KENNEY_DIR_WII,            KENNEY_FILE_WII);
        kf.nintendoGamecube = LoadKenney(KENNEY_DIR_GAMECUBE,       KENNEY_FILE_GAMECUBE);
        kf.keyboardMouse    = LoadKenney(KENNEY_DIR_KEYBOARD_MOUSE, KENNEY_FILE_KEYBOARD_MOUSE);
        kf.steamDeck        = LoadKenney(KENNEY_DIR_STEAM_DECK,     KENNEY_FILE_STEAM_DECK);
        kf.steamController  = LoadKenney(KENNEY_DIR_STEAM_CTRL,     KENNEY_FILE_STEAM_CTRL);
        kf.generic          = LoadKenney(KENNEY_DIR_GENERIC,        KENNEY_FILE_GENERIC);
    }

    io.Fonts->Build();
    theme.ClearPendingFontChange();
}