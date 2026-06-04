#include "App/Log.h"
#include "ThemeManager.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_map>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace InputBridge;

// ============================================================================
//  Singleton
// ============================================================================

ThemeManager& ThemeManager::GetInstance() {
    static ThemeManager instance;
    return instance;
}

// ============================================================================
//  Discovery
// ============================================================================

void ThemeManager::ScanThemesDirectory(const std::string& basePath) {
    m_scanBasePath = basePath;
    m_entries.clear();

    fs::path themesDir = fs::path(basePath) / "themes";
    if (!fs::exists(themesDir) || !fs::is_directory(themesDir)) {
        LOG_INFO("ThemeManager", "Themes directory not found: %s", themesDir.string().c_str());
        return;
    }

    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(themesDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());

    for (const auto& p : found) {
        ThemeEntry te;
        te.path        = p.string();
        te.displayName = PeekDisplayName(te.path);
        if (te.displayName.empty())
            te.displayName = p.stem().string();
        m_entries.push_back(std::move(te));
        LOG_INFO("ThemeManager", "Found theme: %s (%s)",
                m_entries.back().displayName.c_str(),
                m_entries.back().path.c_str());
    }

    LOG_INFO("ThemeManager", "Scanned %d theme(s) from %s",
            (int)m_entries.size(), themesDir.string().c_str());

    // Refresh the index pointer if the active theme is still present.
    if (m_hasCustomTheme && !m_themePath.empty()) {
        m_currentEntryIndex = -1;
        for (int i = 0; i < (int)m_entries.size(); ++i) {
            if (m_entries[i].path == m_themePath) {
                m_currentEntryIndex = i;
                break;
            }
        }
    }
}

void ThemeManager::Refresh() {
    if (!m_scanBasePath.empty())
        ScanThemesDirectory(m_scanBasePath);
}

// ============================================================================
//  Font resolution
// ============================================================================

void ThemeManager::ResolveFontPath(const ThemeData& data) {
    if (!data.hasFontSpec || data.fontFile.empty()) {
        m_resolvedFontPath.clear();
        m_fontSize = 16.f;
        return;
    }

    // Font paths are relative to the executable directory.
    fs::path resolved = fs::path(m_scanBasePath) / data.fontFile;
    if (fs::exists(resolved) && fs::is_regular_file(resolved)) {
        m_resolvedFontPath = resolved.string();
        m_fontSize         = data.fontSize > 0.f ? data.fontSize : 16.f;
        LOG_INFO("ThemeManager", "Font resolved: %s @ %.1fpx",
                m_resolvedFontPath.c_str(), m_fontSize);
    } else {
        // File not found — fall back to ImGui default silently.
        LOG_INFO("ThemeManager", "Font file not found (%s) — using default font.",
                resolved.string().c_str());
        m_resolvedFontPath.clear();
        m_fontSize = 16.f;
    }
}

// ============================================================================
//  Loading / applying
// ============================================================================

Result<bool, std::string> ThemeManager::LoadFromFile(const std::string& path) {
    auto result = ParseFile(path);
    if (result.IsErr()) {
        m_lastError = result.Error();
        LOG_INFO("ThemeManager", "Failed to load theme '%s': %s",
                path.c_str(), m_lastError.c_str());
        return Result<bool, std::string>::Err(m_lastError);
    }

    m_lastError.clear();
    m_current        = result.Value();
    m_themeName      = m_current.name.empty() ? "Custom" : m_current.name;
    m_themePath      = path;
    m_hasCustomTheme = true;

    m_currentEntryIndex = -1;
    for (int i = 0; i < (int)m_entries.size(); ++i) {
        if (m_entries[i].path == path) {
            m_currentEntryIndex = i;
            break;
        }
    }

    ApplyData(m_current);
    ResolveFontPath(m_current);
    m_pendingFontChange = true;

    LOG_INFO("ThemeManager", "Applied theme '%s' from '%s'",
            m_themeName.c_str(), path.c_str());
    return Result<bool, std::string>::Ok(true);
}

void ThemeManager::ApplyDefault() {
    m_hasCustomTheme    = false;
    m_useDefaultLight   = false;
    m_currentEntryIndex = -1;
    m_themeName         = "Default (Dark)";
    m_themePath.clear();
    m_lastError.clear();
    m_current = ThemeData{};
    // Reset font to built-in default.
    m_resolvedFontPath.clear();
    m_fontSize          = 16.f;
    m_pendingFontChange = true;
    // Caller is responsible for calling ImGui::StyleColorsDark() first.
}

void ThemeManager::ApplyDefaultLight() {
    m_hasCustomTheme    = false;
    m_useDefaultLight   = true;
    m_currentEntryIndex = -1;
    m_themeName         = "Default (Light)";
    m_themePath.clear();
    m_lastError.clear();
    m_current = ThemeData{};
    // Reset font to built-in default.
    m_resolvedFontPath.clear();
    m_fontSize          = 16.f;
    m_pendingFontChange = true;
    // Caller is responsible for calling ImGui::StyleColorsLight() first.
}

void ThemeManager::ApplyBaseColors() {
    if (m_useDefaultLight)
        ImGui::StyleColorsLight();
    else
        ImGui::StyleColorsDark();
}

void ThemeManager::Reapply() {
    if (m_hasCustomTheme)
        ApplyData(m_current);
    // Default variants: nothing to overlay — StyleColorsDark/Light() from caller is enough.
}

// ============================================================================
//  Persistence
// ============================================================================

// Sanitize a path that may have been corrupted by repeated escape/unescape
// cycles.  Each save-without-unescape doubles the backslashes, so after N bad
// round-trips a single '\' becomes 2^N backslashes.  We fix this by:
//   1. Replacing every '\' with '/' (Windows accepts '/' as path separator).
//   2. Collapsing every run of '/' into a single '/', while preserving a
//      leading "//" so UNC paths survive.
static std::string SanitizeThemePath(const std::string& raw) {
    // Step 1 – replace all backslashes with forward slashes
    std::string s = raw;
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    // Step 2 – collapse consecutive forward slashes
    // Keep the first two characters unchanged so "C:/" and "//" are preserved.
    std::string out;
    out.reserve(s.size());
    bool prev_slash = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/') {
            if (!prev_slash || i < 2)   // always keep the first two chars as-is
                out += '/';
            prev_slash = true;
        } else {
            out += s[i];
            prev_slash = false;
        }
    }
    return out;
}

void ThemeManager::LoadFromPreferences(PreferencesManager& prefs) {
    std::string path = prefs.GetString("Theme", "ThemePath", "");

    // ── Sentinel for Default (Light) ─────────────────────────────────────────
    if (path == "__default_light__") {
        ApplyDefaultLight();
        return;
    }

    // ── No saved preference → auto-load Resonite as the default theme ────────
    if (path.empty()) {
        if (!m_scanBasePath.empty()) {
            fs::path resonitePath = fs::path(m_scanBasePath) / "themes" / "resonite.json";
            if (fs::exists(resonitePath)) {
                auto result = LoadFromFile(resonitePath.string());
                if (result.IsOk()) {
                    LOG_INFO("ThemeManager", "No saved theme — applied Resonite as default.");
                    return;
                }
            }
        }
        // Resonite not found → silently stay on the built-in dark default.
        return;
    }

    // Recover from any previously over-escaped paths (repeated double-escaping
    // turns one backslash into 2^N backslashes after N bad round-trips).
    path = SanitizeThemePath(path);

    auto result = LoadFromFile(path);
    if (result.IsErr()) {
        LOG_INFO("ThemeManager", "Saved theme could not be restored: %s — using default.",
                result.Error().c_str());
        prefs.DeleteKey("Theme", "ThemePath");
    }
}

void ThemeManager::SaveToPreferences(PreferencesManager& prefs) const {
    if (m_hasCustomTheme) {
        // Always persist with forward slashes so the TOML escape/unescape cycle
        // never has to deal with backslashes (which it historically doubled).
        prefs.SetString("Theme", "ThemePath", SanitizeThemePath(m_themePath));
    } else if (m_useDefaultLight) {
        // Persist the light default selection via a sentinel value.
        prefs.SetString("Theme", "ThemePath", "__default_light__");
    } else {
        prefs.DeleteKey("Theme", "ThemePath");
    }
}

// ============================================================================
//  Internal – colour-name → ImGuiCol_ index
// ============================================================================

int ThemeManager::ColorNameToIndex(const std::string& name) {
    static const std::unordered_map<std::string, int> table = {
        {"Text",                   ImGuiCol_Text},
        {"TextDisabled",           ImGuiCol_TextDisabled},
        {"WindowBg",               ImGuiCol_WindowBg},
        {"ChildBg",                ImGuiCol_ChildBg},
        {"PopupBg",                ImGuiCol_PopupBg},
        {"Border",                 ImGuiCol_Border},
        {"BorderShadow",           ImGuiCol_BorderShadow},
        {"FrameBg",                ImGuiCol_FrameBg},
        {"FrameBgHovered",         ImGuiCol_FrameBgHovered},
        {"FrameBgActive",          ImGuiCol_FrameBgActive},
        {"TitleBg",                ImGuiCol_TitleBg},
        {"TitleBgActive",          ImGuiCol_TitleBgActive},
        {"TitleBgCollapsed",       ImGuiCol_TitleBgCollapsed},
        {"MenuBarBg",              ImGuiCol_MenuBarBg},
        {"ScrollbarBg",            ImGuiCol_ScrollbarBg},
        {"ScrollbarGrab",          ImGuiCol_ScrollbarGrab},
        {"ScrollbarGrabHovered",   ImGuiCol_ScrollbarGrabHovered},
        {"ScrollbarGrabActive",    ImGuiCol_ScrollbarGrabActive},
        {"CheckMark",              ImGuiCol_CheckMark},
        {"SliderGrab",             ImGuiCol_SliderGrab},
        {"SliderGrabActive",       ImGuiCol_SliderGrabActive},
        {"Button",                 ImGuiCol_Button},
        {"ButtonHovered",          ImGuiCol_ButtonHovered},
        {"ButtonActive",           ImGuiCol_ButtonActive},
        {"Header",                 ImGuiCol_Header},
        {"HeaderHovered",          ImGuiCol_HeaderHovered},
        {"HeaderActive",           ImGuiCol_HeaderActive},
        {"Separator",              ImGuiCol_Separator},
        {"SeparatorHovered",       ImGuiCol_SeparatorHovered},
        {"SeparatorActive",        ImGuiCol_SeparatorActive},
        {"ResizeGrip",             ImGuiCol_ResizeGrip},
        {"ResizeGripHovered",      ImGuiCol_ResizeGripHovered},
        {"ResizeGripActive",       ImGuiCol_ResizeGripActive},
        {"Tab",                    ImGuiCol_Tab},
        {"TabHovered",             ImGuiCol_TabHovered},
        {"TabActive",              ImGuiCol_TabActive},
        {"TabUnfocused",           ImGuiCol_TabUnfocused},
        {"TabUnfocusedActive",     ImGuiCol_TabUnfocusedActive},
        {"DockingPreview",         ImGuiCol_DockingPreview},
        {"DockingEmptyBg",         ImGuiCol_DockingEmptyBg},
        {"PlotLines",              ImGuiCol_PlotLines},
        {"PlotLinesHovered",       ImGuiCol_PlotLinesHovered},
        {"PlotHistogram",          ImGuiCol_PlotHistogram},
        {"PlotHistogramHovered",   ImGuiCol_PlotHistogramHovered},
        {"TableHeaderBg",          ImGuiCol_TableHeaderBg},
        {"TableBorderStrong",      ImGuiCol_TableBorderStrong},
        {"TableBorderLight",       ImGuiCol_TableBorderLight},
        {"TableRowBg",             ImGuiCol_TableRowBg},
        {"TableRowBgAlt",          ImGuiCol_TableRowBgAlt},
        {"TextSelectedBg",         ImGuiCol_TextSelectedBg},
        {"DragDropTarget",         ImGuiCol_DragDropTarget},
        {"NavHighlight",           ImGuiCol_NavHighlight},
        {"NavWindowingHighlight",  ImGuiCol_NavWindowingHighlight},
        {"NavWindowingDimBg",      ImGuiCol_NavWindowingDimBg},
        {"ModalWindowDimBg",       ImGuiCol_ModalWindowDimBg},
    };
    auto it = table.find(name);
    return (it != table.end()) ? it->second : -1;
}

// ============================================================================
//  Internal – peek at a file's "name" field without full validation
// ============================================================================

std::string ThemeManager::PeekDisplayName(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    try {
        json doc;
        f >> doc;
        if (doc.is_object() && doc.contains("name") && doc["name"].is_string()) {
            std::string n = doc["name"].get<std::string>();
            if (n.size() <= 128) return n;
        }
    } catch (...) {}
    return "";
}

// ============================================================================
//  Internal – full JSON parse + validation
// ============================================================================

Result<ThemeManager::ThemeData, std::string>
ThemeManager::ParseFile(const std::string& path) const {
    std::ifstream file(path);
    if (!file.is_open())
        return Result<ThemeData, std::string>::Err("Cannot open file: " + path);

    json doc;
    try {
        file >> doc;
    } catch (const json::parse_error& e) {
        return Result<ThemeData, std::string>::Err(
            std::string("JSON parse error: ") + e.what());
    }

    if (!doc.is_object())
        return Result<ThemeData, std::string>::Err(
            "Theme file must be a JSON object at the root level.");

    if (!doc.contains("colors") || !doc["colors"].is_object())
        return Result<ThemeData, std::string>::Err(
            "Theme file is missing the required \"colors\" object.");

    ThemeData data;

    // Optional name
    if (doc.contains("name") && doc["name"].is_string()) {
        data.name = doc["name"].get<std::string>();
        if (data.name.size() > 128)
            return Result<ThemeData, std::string>::Err(
                "\"name\" field exceeds 128 characters.");
    }

    // Colours
    const auto& colorsObj = doc["colors"];
    int parsedCount = 0;
    for (auto it = colorsObj.begin(); it != colorsObj.end(); ++it) {
        const std::string& colorName = it.key();
        const json& val = it.value();

        int idx = ColorNameToIndex(colorName);
        if (idx < 0) continue; // unknown key — forward-compat

        if (!val.is_array() || val.size() != 4)
            return Result<ThemeData, std::string>::Err(
                "Color \"" + colorName + "\" must be an array of exactly 4 numbers [r,g,b,a].");

        for (int ch = 0; ch < 4; ++ch) {
            if (!val[ch].is_number())
                return Result<ThemeData, std::string>::Err(
                    "Color \"" + colorName + "\": channel " + std::to_string(ch) + " is not a number.");
            float v = val[ch].get<float>();
            if (v < 0.0f || v > 1.0f)
                return Result<ThemeData, std::string>::Err(
                    "Color \"" + colorName + "\": channel " + std::to_string(ch) +
                    " value " + std::to_string(v) + " is out of range [0.0, 1.0].");
            data.colors[idx][ch] = v;
        }
        data.colorSet[idx] = true;
        ++parsedCount;
    }

    if (parsedCount == 0)
        return Result<ThemeData, std::string>::Err(
            "\"colors\" object contains no recognised ImGui colour names.");

    // Optional style overrides
    if (doc.contains("style") && doc["style"].is_object()) {
        const auto& s = doc["style"];
        auto rf = [&](const char* k, bool& flag, float& out) -> std::string {
            if (!s.contains(k)) return "";
            if (!s[k].is_number())
                return std::string("style.") + k + " must be a number.";
            out = s[k].get<float>(); flag = true; return "";
        };
        std::string err;
        if (!(err = rf("WindowRounding",    data.hasWindowRounding,    data.windowRounding)).empty())    return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("FrameRounding",     data.hasFrameRounding,     data.frameRounding)).empty())     return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("ScrollbarRounding", data.hasScrollbarRounding, data.scrollbarRounding)).empty()) return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("GrabRounding",      data.hasGrabRounding,      data.grabRounding)).empty())      return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("TabRounding",       data.hasTabRounding,       data.tabRounding)).empty())       return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("WindowBorderSize",  data.hasWindowBorderSize,  data.windowBorderSize)).empty())  return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("FrameBorderSize",   data.hasFrameBorderSize,   data.frameBorderSize)).empty())   return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("PopupRounding",     data.hasPopupRounding,     data.popupRounding)).empty())     return Result<ThemeData,std::string>::Err(err);
        if (!(err = rf("ChildRounding",     data.hasChildRounding,     data.childRounding)).empty())     return Result<ThemeData,std::string>::Err(err);
    }

    // Optional font spec — no error if missing or file doesn't exist (fallback handled in ResolveFontPath)
    if (doc.contains("font") && doc["font"].is_object()) {
        const auto& f = doc["font"];
        if (f.contains("file") && f["file"].is_string()) {
            data.fontFile   = f["file"].get<std::string>();
            data.hasFontSpec = true;
        }
        if (f.contains("size") && f["size"].is_number()) {
            float sz = f["size"].get<float>();
            if (sz < 4.f || sz > 144.f)
                return Result<ThemeData, std::string>::Err(
                    "font.size must be between 4 and 144 pixels.");
            data.fontSize = sz;
        }
    }

    data.path = path;
    return Result<ThemeData, std::string>::Ok(std::move(data));
}

// ============================================================================
//  Internal – write ThemeData into live ImGuiStyle
// ============================================================================

void ThemeManager::ApplyData(const ThemeData& data) {
    ImGuiStyle& style = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        if (data.colorSet[i])
            style.Colors[i] = ImVec4(data.colors[i][0], data.colors[i][1],
                                     data.colors[i][2], data.colors[i][3]);
    }
    if (data.hasWindowRounding)    style.WindowRounding    = data.windowRounding;
    if (data.hasFrameRounding)     style.FrameRounding     = data.frameRounding;
    if (data.hasScrollbarRounding) style.ScrollbarRounding = data.scrollbarRounding;
    if (data.hasGrabRounding)      style.GrabRounding      = data.grabRounding;
    if (data.hasTabRounding)       style.TabRounding       = data.tabRounding;
    if (data.hasWindowBorderSize)  style.WindowBorderSize  = data.windowBorderSize;
    if (data.hasFrameBorderSize)   style.FrameBorderSize   = data.frameBorderSize;
    if (data.hasPopupRounding)     style.PopupRounding     = data.popupRounding;
    if (data.hasChildRounding)     style.ChildRounding     = data.childRounding;
}