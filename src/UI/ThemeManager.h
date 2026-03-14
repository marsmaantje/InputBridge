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
 *     "name":   "My Theme",
 *     "colors": {                    // ImGuiCol_ name → [r, g, b, a]  (0..1)
 *       "WindowBg": [0.06, 0.06, 0.06, 0.94],
 *       ...
 *     },
 *     "style": {                     // optional rounding / border overrides
 *       "WindowRounding": 5.0,
 *       "FrameRounding":  3.0,
 *       ...
 *     },
 *     "font": {                      // optional — omit to keep the ImGui default
 *       "file": "fonts/MyFont.ttf",  // path relative to the executable directory
 *       "size": 16.0                 // size in pixels (before UI scale)
 *     }
 *   }
 *
 * Font fallback
 * -------------
 * If "font" is present but the file cannot be opened (missing, bad path, …),
 * ThemeManager automatically falls back to the built-in ImGui default font.
 * No error is raised for an unloadable font; the font path is simply cleared.
 *
 * Font rebuild workflow (owned by main.cpp)
 * -----------------------------------------
 * After any call to LoadFromFile() or ApplyDefault() that changes the font
 * selection, HasPendingFontChange() returns true.  The caller must then, before
 * the next ImGui_ImplSDLRenderer3_NewFrame() call:
 *   1. Destroy the existing font texture  (ImGui_ImplSDLRenderer3_DestroyFontsTexture)
 *   2. Clear and repopulate io.Fonts
 *   3. Call io.Fonts->Build() and ImGui_ImplSDLRenderer3_CreateFontsTexture()
 *   4. Call ClearPendingFontChange()
 * Use GetResolvedFontPath() and GetFontSize() for the parameters.
 * An empty GetResolvedFontPath() means "use AddFontDefault()".
 *
 * Colour validation runs entirely before touching live ImGuiStyle, so a
 * broken file leaves the current theme and font unchanged.
 */
class ThemeManager {
public:
    static ThemeManager& GetInstance();

    // -----------------------------------------------------------------------
    // Discovery
    // -----------------------------------------------------------------------

    struct ThemeEntry {
        std::string displayName;  ///< "name" field in JSON, or filename stem
        std::string path;         ///< Full filesystem path to the .json file
    };

    /** Scan <basePath>/themes/ for *.json files. Call once at startup. */
    void ScanThemesDirectory(const std::string& basePath);

    /** Re-scan using the same basePath as the last ScanThemesDirectory call. */
    void Refresh();

    /** All themes found by the last scan (does not include the built-in default). */
    const std::vector<ThemeEntry>& GetAvailableThemes() const { return m_entries; }

    // -----------------------------------------------------------------------
    // Loading / applying
    // -----------------------------------------------------------------------

    /**
     * Load, validate and apply a theme from @p path.
     * On success → Ok(true), applied immediately.
     * On failure → Err(message), previous theme unchanged.
     * Always marks HasPendingFontChange() so the caller can rebuild the atlas.
     */
    InputBridge::Result<bool, std::string> LoadFromFile(const std::string& path);

    /**
     * Restore the built-in ImGui dark theme and default font.
     * Always marks HasPendingFontChange().
     */
    void ApplyDefault();

    /**
     * Re-apply colours and style to the live ImGuiStyle without re-reading disk.
     * Call after any code that resets ImGuiStyle (e.g. UpdateUIScale).
     * Does NOT touch fonts — handle those via HasPendingFontChange().
     */
    void Reapply();

    // -----------------------------------------------------------------------
    // Font support
    // -----------------------------------------------------------------------

    /** True when the font has changed and the atlas must be rebuilt. */
    bool HasPendingFontChange() const { return m_pendingFontChange; }

    /** Call after completing the atlas rebuild. */
    void ClearPendingFontChange() { m_pendingFontChange = false; }

    /**
     * Full filesystem path to the font for the current theme.
     * Empty string → use ImGui's built-in default font (AddFontDefault).
     */
    const std::string& GetResolvedFontPath() const { return m_resolvedFontPath; }

    /**
     * Base font size in pixels (before UI scale) for the current theme.
     * Defaults to 16.0 when no custom font is specified.
     */
    float GetFontSize() const { return m_fontSize; }

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

    const std::string& GetCurrentThemeName()  const { return m_themeName; }
    const std::string& GetCurrentThemePath()  const { return m_themePath; }
    bool               HasCustomTheme()        const { return m_hasCustomTheme; }
    int                GetCurrentEntryIndex()  const { return m_currentEntryIndex; }
    const std::string& GetLastError()          const { return m_lastError; }

private:
    ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    // -----------------------------------------------------------------------
    // Internal
    // -----------------------------------------------------------------------

    struct ThemeData {
        std::string name;
        std::string path;
        // Colours
        float colors[ImGuiCol_COUNT][4]{};
        bool  colorSet[ImGuiCol_COUNT]{};
        // Style overrides
        bool  hasWindowRounding    = false; float windowRounding    = 0.f;
        bool  hasFrameRounding     = false; float frameRounding     = 0.f;
        bool  hasScrollbarRounding = false; float scrollbarRounding = 0.f;
        bool  hasGrabRounding      = false; float grabRounding      = 0.f;
        bool  hasTabRounding       = false; float tabRounding       = 0.f;
        bool  hasWindowBorderSize  = false; float windowBorderSize  = 0.f;
        bool  hasFrameBorderSize   = false; float frameBorderSize   = 0.f;
        bool  hasPopupRounding     = false; float popupRounding     = 0.f;
        bool  hasChildRounding     = false; float childRounding     = 0.f;
        // Font spec (optional)
        bool        hasFontSpec = false;
        std::string fontFile;   ///< relative to executable directory
        float       fontSize    = 16.f;
    };

    InputBridge::Result<ThemeData, std::string> ParseFile(const std::string& path) const;
    static void ApplyData(const ThemeData& data);
    static int  ColorNameToIndex(const std::string& name);
    static std::string PeekDisplayName(const std::string& path);

    /** Resolve font path and populate m_resolvedFontPath / m_fontSize. */
    void ResolveFontPath(const ThemeData& data);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    std::vector<ThemeEntry> m_entries;
    std::string             m_scanBasePath;

    bool        m_hasCustomTheme    = false;
    int         m_currentEntryIndex = -1;
    ThemeData   m_current;
    std::string m_themeName = "Default (Dark)";
    std::string m_themePath;
    std::string m_lastError;

    // Font state
    // Initialised to true so that RebuildFontAtlas() is always called once at
    // startup — even when no custom theme is saved and LoadFromPreferences()
    // returns early without setting the flag.  Without this the FA6 icon font
    // is never merged into the ImGui atlas and all icon glyphs render as '?'.
    bool        m_pendingFontChange = true;
    std::string m_resolvedFontPath;   ///< empty = use built-in default
    float       m_fontSize          = 16.f;
};
