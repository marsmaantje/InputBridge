#pragma once

#include "imgui.h"
#include "Core/Result.h"
#include "Preferences/Preferences.h"
#include <string>

/**
 * ThemeManager
 *
 * Loads, validates, and applies external Dear ImGui colour themes stored as
 * JSON files.  A theme file has the following shape:
 *
 *   {
 *     "name":   "My Theme",          // optional display name
 *     "colors": {                    // required – map of ImGuiCol_ names → [r,g,b,a]
 *       "Text":      [1.0, 1.0, 1.0, 1.0],
 *       "WindowBg":  [0.06, 0.06, 0.06, 0.94],
 *       ...
 *     },
 *     "style": {                     // optional – common rounding/spacing overrides
 *       "WindowRounding":    5.0,
 *       "FrameRounding":     3.0,
 *       "ScrollbarRounding": 3.0,
 *       "GrabRounding":      3.0,
 *       "TabRounding":       4.0,
 *       "WindowBorderSize":  1.0,
 *       "FrameBorderSize":   0.0,
 *       "PopupRounding":     3.0,
 *       "ChildRounding":     3.0
 *     }
 *   }
 *
 * Colour channel values must be in [0.0 … 1.0].  Unknown colour names are
 * silently ignored so that themes written for a newer ImGui version still work
 * on an older build.
 *
 * LoadFromFile() validates the file before touching the live ImGui style.
 * If validation or parsing fails the previous theme (or the built-in dark
 * theme) is restored and an error string is returned.
 *
 * Reapply() must be called whenever the ImGui style is reset (e.g. after
 * UpdateUIScale resets it to the default) so that the custom colours are
 * re-applied on top of the fresh style.
 */
class ThemeManager {
public:
    static ThemeManager& GetInstance();

    // -----------------------------------------------------------------------
    // Loading
    // -----------------------------------------------------------------------

    /**
     * Load, validate and apply a theme from @p path.
     *
     * On success  → returns Ok(true), theme is applied immediately.
     * On failure  → returns Err(message), previous theme is kept unchanged.
     */
    InputBridge::Result<bool, std::string> LoadFromFile(const std::string& path);

    /**
     * Restore the built-in ImGui dark theme and forget any loaded file.
     */
    void ApplyDefault();

    /**
     * Re-apply the currently active theme without re-reading the file.
     * Call this after any code that resets ImGuiStyle (e.g. UpdateUIScale).
     */
    void Reapply();

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------

    /** Read "ThemePath" from preferences and load it at startup. */
    void LoadFromPreferences(PreferencesManager& prefs);

    /** Persist the current theme path to preferences. */
    void SaveToPreferences(PreferencesManager& prefs) const;

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    /** Display name from the theme file, or "Default". */
    const std::string& GetCurrentThemeName() const { return m_themeName; }

    /** File path of the loaded theme file, or empty string for the default. */
    const std::string& GetCurrentThemePath() const { return m_themePath; }

    /** True when a custom theme file is currently active. */
    bool HasCustomTheme() const { return m_hasCustomTheme; }

    /** Last error produced by LoadFromFile(), cleared on next successful load. */
    const std::string& GetLastError() const { return m_lastError; }

private:
    ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    struct ThemeData {
        std::string name;
        std::string path;
        // Per-colour RGBA values in [0,1]. Index = ImGuiCol_ enum value.
        // A flag array tracks which entries were actually supplied.
        float  colors[ImGuiCol_COUNT][4]{};
        bool   colorSet[ImGuiCol_COUNT]{};
        // Optional style overrides
        bool   hasWindowRounding    = false; float windowRounding    = 0.f;
        bool   hasFrameRounding     = false; float frameRounding     = 0.f;
        bool   hasScrollbarRounding = false; float scrollbarRounding = 0.f;
        bool   hasGrabRounding      = false; float grabRounding      = 0.f;
        bool   hasTabRounding       = false; float tabRounding       = 0.f;
        bool   hasWindowBorderSize  = false; float windowBorderSize  = 0.f;
        bool   hasFrameBorderSize   = false; float frameBorderSize   = 0.f;
        bool   hasPopupRounding     = false; float popupRounding     = 0.f;
        bool   hasChildRounding     = false; float childRounding     = 0.f;
    };

    InputBridge::Result<ThemeData, std::string> ParseFile(const std::string& path) const;
    static void ApplyData(const ThemeData& data);
    static int  ColorNameToIndex(const std::string& name);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    bool        m_hasCustomTheme = false;
    ThemeData   m_current;
    std::string m_themeName = "Default";
    std::string m_themePath;
    std::string m_lastError;
};