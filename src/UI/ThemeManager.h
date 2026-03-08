#pragma once

#include "imgui.h"
#include "Core/Result.h"
#include "Preferences/Preferences.h"
#include <string>
#include <vector>

/**
 * ThemeManager
 *
 * Scans a "themes/" sub-folder next to the executable and exposes all
 * discovered .json files as selectable colour themes.
 *
 * Theme file format
 * -----------------
 *   {
 *     "name":   "My Theme",          // display name shown in the dropdown
 *     "colors": {                    // ImGuiCol_ name → [r, g, b, a]  (values 0..1)
 *       "WindowBg":  [0.06, 0.06, 0.06, 0.94],
 *       "Button":    [0.20, 0.40, 0.80, 1.00],
 *       ...
 *     },
 *     "style": {                     // optional rounding / border overrides
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
 * Workflow
 * --------
 *  1. At startup call ScanThemesDirectory() – fills GetAvailableThemes().
 *  2. Call LoadFromPreferences() to restore the last used selection.
 *  3. Show a combo box driven by GetAvailableThemes(); on selection change
 *     call LoadFromFile(entry.path) or ApplyDefault().
 *  4. Call Reapply() whenever ImGuiStyle is reset (e.g. inside UpdateUIScale).
 *
 * Validation runs entirely before touching the live ImGui style, so a broken
 * file leaves the current theme intact and returns an error string.
 */
class ThemeManager {
public:
    static ThemeManager& GetInstance();

    // -----------------------------------------------------------------------
    // Discovery
    // -----------------------------------------------------------------------

    /** Metadata for one discovered theme file. */
    struct ThemeEntry {
        std::string displayName;   ///< From "name" field in JSON, or filename stem
        std::string path;          ///< Full filesystem path to the .json file
    };

    /**
     * Scan <basePath>/themes/ for *.json files and populate the internal list.
     * Call once at startup (and again on "Refresh").
     * @param basePath  Directory that contains the "themes" sub-folder.
     *                  Typically SDL_GetBasePath().
     */
    void ScanThemesDirectory(const std::string& basePath);

    /** Re-scan using the same basePath provided to the last ScanThemesDirectory call. */
    void Refresh();

    /** All themes discovered by the last scan (not including the built-in default). */
    const std::vector<ThemeEntry>& GetAvailableThemes() const { return m_entries; }

    // -----------------------------------------------------------------------
    // Loading / applying
    // -----------------------------------------------------------------------

    /**
     * Load, validate and apply a theme from @p path.
     * On success → Ok(true), applied immediately.
     * On failure → Err(message), previous theme unchanged.
     */
    InputBridge::Result<bool, std::string> LoadFromFile(const std::string& path);

    /** Restore the built-in dark theme and forget any loaded file. */
    void ApplyDefault();

    /**
     * Re-apply the active theme without re-reading disk.
     * Must be called after any code that resets ImGuiStyle (e.g. UpdateUIScale).
     */
    void Reapply();

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------

    /** Restore the last used theme from preferences (called once at startup). */
    void LoadFromPreferences(PreferencesManager& prefs);

    /** Persist the active theme selection to preferences. */
    void SaveToPreferences(PreferencesManager& prefs) const;

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    /** Display name of the active theme, or "Default (Dark)". */
    const std::string& GetCurrentThemeName() const { return m_themeName; }

    /** Full path of the active theme file, or empty for the built-in default. */
    const std::string& GetCurrentThemePath() const { return m_themePath; }

    /** True when a custom file theme is active. */
    bool HasCustomTheme() const { return m_hasCustomTheme; }

    /**
     * Index into GetAvailableThemes() for the active theme, or -1 (= Default).
     * Suitable for use directly as a combo-box selection index when offset by 1
     * (index 0 = Default, index 1..N = entries[0..N-1]).
     */
    int GetCurrentEntryIndex() const { return m_currentEntryIndex; }

    /** Last error from LoadFromFile(), cleared on next successful load. */
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
        float colors[ImGuiCol_COUNT][4]{};
        bool  colorSet[ImGuiCol_COUNT]{};
        bool  hasWindowRounding    = false; float windowRounding    = 0.f;
        bool  hasFrameRounding     = false; float frameRounding     = 0.f;
        bool  hasScrollbarRounding = false; float scrollbarRounding = 0.f;
        bool  hasGrabRounding      = false; float grabRounding      = 0.f;
        bool  hasTabRounding       = false; float tabRounding       = 0.f;
        bool  hasWindowBorderSize  = false; float windowBorderSize  = 0.f;
        bool  hasFrameBorderSize   = false; float frameBorderSize   = 0.f;
        bool  hasPopupRounding     = false; float popupRounding     = 0.f;
        bool  hasChildRounding     = false; float childRounding     = 0.f;
    };

    InputBridge::Result<ThemeData, std::string> ParseFile(const std::string& path) const;
    static void ApplyData(const ThemeData& data);
    static int  ColorNameToIndex(const std::string& name);
    /// Peek at just the "name" field from a JSON file without full validation.
    static std::string PeekDisplayName(const std::string& path);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    std::vector<ThemeEntry> m_entries;
    std::string             m_scanBasePath;

    bool        m_hasCustomTheme    = false;
    int         m_currentEntryIndex = -1;   ///< -1 = Default
    ThemeData   m_current;
    std::string m_themeName = "Default (Dark)";
    std::string m_themePath;
    std::string m_lastError;
};
