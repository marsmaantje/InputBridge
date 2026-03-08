#include "ThemeManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <SDL3/SDL_log.h>

using json = nlohmann::json;
using namespace InputBridge;

// ============================================================================
//  Singleton
// ============================================================================

ThemeManager& ThemeManager::GetInstance() {
    static ThemeManager instance;
    return instance;
}

// ============================================================================
//  Public API
// ============================================================================

Result<bool, std::string> ThemeManager::LoadFromFile(const std::string& path) {
    auto result = ParseFile(path);
    if (result.IsErr()) {
        m_lastError = result.Error();
        SDL_Log("[ThemeManager] Failed to load theme '%s': %s",
                path.c_str(), m_lastError.c_str());
        // Keep the current theme unchanged; nothing has been written to ImGui yet.
        return Result<bool, std::string>::Err(m_lastError);
    }

    m_lastError.clear();
    m_current        = result.Value();
    m_themeName      = m_current.name.empty() ? "Custom" : m_current.name;
    m_themePath      = path;
    m_hasCustomTheme = true;

    ApplyData(m_current);
    SDL_Log("[ThemeManager] Applied theme '%s' from '%s'",
            m_themeName.c_str(), path.c_str());
    return Result<bool, std::string>::Ok(true);
}

void ThemeManager::ApplyDefault() {
    m_hasCustomTheme = false;
    m_themeName      = "Default";
    m_themePath.clear();
    m_lastError.clear();
    m_current        = ThemeData{};
    // The caller is expected to have reset the style already (e.g. via
    // ImGui::StyleColorsDark()), so there is nothing else to do here.
}

void ThemeManager::Reapply() {
    if (m_hasCustomTheme) {
        ApplyData(m_current);
    }
    // Default theme: nothing to do — caller already called StyleColorsDark().
}

// -----------------------------------------------------------------------
//  Persistence
// -----------------------------------------------------------------------

void ThemeManager::LoadFromPreferences(PreferencesManager& prefs) {
    std::string path = prefs.GetString("Theme", "ThemePath", "");
    if (path.empty()) return;

    auto result = LoadFromFile(path);
    if (result.IsErr()) {
        SDL_Log("[ThemeManager] Saved theme could not be restored: %s — using default.",
                result.Error().c_str());
        // Silently fall back to the default (ApplyDefault was not called because
        // we never touched the live style, so the caller's StyleColorsDark()
        // remains in effect).
        prefs.DeleteKey("Theme", "ThemePath");
    }
}

void ThemeManager::SaveToPreferences(PreferencesManager& prefs) const {
    if (m_hasCustomTheme) {
        prefs.SetString("Theme", "ThemePath", m_themePath);
    } else {
        prefs.DeleteKey("Theme", "ThemePath");
    }
}

// ============================================================================
//  Internal – colour-name → ImGuiCol_ index mapping
// ============================================================================

int ThemeManager::ColorNameToIndex(const std::string& name) {
    // Generated from imgui.h ImGuiCol_ enum (as of Dear ImGui 1.9x).
    // Unknown names return -1 and are silently ignored.
    static const std::unordered_map<std::string, int> table = {
        {"Text",                        ImGuiCol_Text},
        {"TextDisabled",                ImGuiCol_TextDisabled},
        {"WindowBg",                    ImGuiCol_WindowBg},
        {"ChildBg",                     ImGuiCol_ChildBg},
        {"PopupBg",                     ImGuiCol_PopupBg},
        {"Border",                      ImGuiCol_Border},
        {"BorderShadow",                ImGuiCol_BorderShadow},
        {"FrameBg",                     ImGuiCol_FrameBg},
        {"FrameBgHovered",              ImGuiCol_FrameBgHovered},
        {"FrameBgActive",               ImGuiCol_FrameBgActive},
        {"TitleBg",                     ImGuiCol_TitleBg},
        {"TitleBgActive",               ImGuiCol_TitleBgActive},
        {"TitleBgCollapsed",            ImGuiCol_TitleBgCollapsed},
        {"MenuBarBg",                   ImGuiCol_MenuBarBg},
        {"ScrollbarBg",                 ImGuiCol_ScrollbarBg},
        {"ScrollbarGrab",               ImGuiCol_ScrollbarGrab},
        {"ScrollbarGrabHovered",        ImGuiCol_ScrollbarGrabHovered},
        {"ScrollbarGrabActive",         ImGuiCol_ScrollbarGrabActive},
        {"CheckMark",                   ImGuiCol_CheckMark},
        {"SliderGrab",                  ImGuiCol_SliderGrab},
        {"SliderGrabActive",            ImGuiCol_SliderGrabActive},
        {"Button",                      ImGuiCol_Button},
        {"ButtonHovered",               ImGuiCol_ButtonHovered},
        {"ButtonActive",                ImGuiCol_ButtonActive},
        {"Header",                      ImGuiCol_Header},
        {"HeaderHovered",               ImGuiCol_HeaderHovered},
        {"HeaderActive",                ImGuiCol_HeaderActive},
        {"Separator",                   ImGuiCol_Separator},
        {"SeparatorHovered",            ImGuiCol_SeparatorHovered},
        {"SeparatorActive",             ImGuiCol_SeparatorActive},
        {"ResizeGrip",                  ImGuiCol_ResizeGrip},
        {"ResizeGripHovered",           ImGuiCol_ResizeGripHovered},
        {"ResizeGripActive",            ImGuiCol_ResizeGripActive},
        {"Tab",                         ImGuiCol_Tab},
        {"TabHovered",                  ImGuiCol_TabHovered},
        {"TabActive",                   ImGuiCol_TabActive},
        {"TabUnfocused",                ImGuiCol_TabUnfocused},
        {"TabUnfocusedActive",          ImGuiCol_TabUnfocusedActive},
        {"DockingPreview",              ImGuiCol_DockingPreview},
        {"DockingEmptyBg",              ImGuiCol_DockingEmptyBg},
        {"PlotLines",                   ImGuiCol_PlotLines},
        {"PlotLinesHovered",            ImGuiCol_PlotLinesHovered},
        {"PlotHistogram",               ImGuiCol_PlotHistogram},
        {"PlotHistogramHovered",        ImGuiCol_PlotHistogramHovered},
        {"TableHeaderBg",               ImGuiCol_TableHeaderBg},
        {"TableBorderStrong",           ImGuiCol_TableBorderStrong},
        {"TableBorderLight",            ImGuiCol_TableBorderLight},
        {"TableRowBg",                  ImGuiCol_TableRowBg},
        {"TableRowBgAlt",               ImGuiCol_TableRowBgAlt},
        {"TextSelectedBg",              ImGuiCol_TextSelectedBg},
        {"DragDropTarget",              ImGuiCol_DragDropTarget},
        {"NavHighlight",                ImGuiCol_NavHighlight},
        {"NavWindowingHighlight",       ImGuiCol_NavWindowingHighlight},
        {"NavWindowingDimBg",           ImGuiCol_NavWindowingDimBg},
        {"ModalWindowDimBg",            ImGuiCol_ModalWindowDimBg},
    };

    auto it = table.find(name);
    return (it != table.end()) ? it->second : -1;
}

// ============================================================================
//  Internal – JSON parsing + validation
// ============================================================================

Result<ThemeManager::ThemeData, std::string>
ThemeManager::ParseFile(const std::string& path) const {
    // ----- open file --------------------------------------------------------
    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<ThemeData, std::string>::Err(
            "Cannot open file: " + path);
    }

    // ----- parse JSON -------------------------------------------------------
    json doc;
    try {
        file >> doc;
    } catch (const json::parse_error& e) {
        return Result<ThemeData, std::string>::Err(
            std::string("JSON parse error: ") + e.what());
    }

    if (!doc.is_object()) {
        return Result<ThemeData, std::string>::Err(
            "Theme file must be a JSON object at the root level.");
    }

    // ----- require "colors" section -----------------------------------------
    if (!doc.contains("colors") || !doc["colors"].is_object()) {
        return Result<ThemeData, std::string>::Err(
            "Theme file is missing the required \"colors\" object.");
    }

    ThemeData data;

    // ----- optional display name -------------------------------------------
    if (doc.contains("name") && doc["name"].is_string()) {
        data.name = doc["name"].get<std::string>();
        if (data.name.size() > 128) {
            return Result<ThemeData, std::string>::Err(
                "\"name\" field exceeds 128 characters.");
        }
    }

    // ----- colours ---------------------------------------------------------
    const auto& colorsObj = doc["colors"];
    int parsedCount = 0;

    for (auto it = colorsObj.begin(); it != colorsObj.end(); ++it) {
        const std::string& colorName = it.key();
        const json& val = it.value();

        int idx = ColorNameToIndex(colorName);
        if (idx < 0) {
            // Unknown name – skip silently for forward-compat.
            continue;
        }

        if (!val.is_array() || val.size() != 4) {
            return Result<ThemeData, std::string>::Err(
                "Color \"" + colorName + "\" must be an array of exactly 4 numbers [r, g, b, a].");
        }

        for (int ch = 0; ch < 4; ++ch) {
            if (!val[ch].is_number()) {
                return Result<ThemeData, std::string>::Err(
                    "Color \"" + colorName + "\": channel " + std::to_string(ch) +
                    " is not a number.");
            }
            float v = val[ch].get<float>();
            if (v < 0.0f || v > 1.0f) {
                return Result<ThemeData, std::string>::Err(
                    "Color \"" + colorName + "\": channel " + std::to_string(ch) +
                    " value " + std::to_string(v) +
                    " is out of range [0.0, 1.0].");
            }
            data.colors[idx][ch] = v;
        }
        data.colorSet[idx] = true;
        ++parsedCount;
    }

    if (parsedCount == 0) {
        return Result<ThemeData, std::string>::Err(
            "Theme file \"colors\" object contains no recognised ImGui colour names.");
    }

    // ----- optional style overrides ----------------------------------------
    if (doc.contains("style") && doc["style"].is_object()) {
        const auto& styleObj = doc["style"];

        auto readFloat = [&](const char* key, bool& flag, float& out) -> std::string {
            if (!styleObj.contains(key)) return "";
            if (!styleObj[key].is_number()) {
                return std::string("style.") + key + " must be a number.";
            }
            out  = styleObj[key].get<float>();
            flag = true;
            return "";
        };

        std::string err;
        if (!(err = readFloat("WindowRounding",    data.hasWindowRounding,    data.windowRounding)).empty())    return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("FrameRounding",     data.hasFrameRounding,     data.frameRounding)).empty())     return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("ScrollbarRounding", data.hasScrollbarRounding, data.scrollbarRounding)).empty()) return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("GrabRounding",      data.hasGrabRounding,      data.grabRounding)).empty())      return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("TabRounding",       data.hasTabRounding,       data.tabRounding)).empty())       return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("WindowBorderSize",  data.hasWindowBorderSize,  data.windowBorderSize)).empty())  return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("FrameBorderSize",   data.hasFrameBorderSize,   data.frameBorderSize)).empty())   return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("PopupRounding",     data.hasPopupRounding,     data.popupRounding)).empty())     return Result<ThemeData, std::string>::Err(err);
        if (!(err = readFloat("ChildRounding",     data.hasChildRounding,     data.childRounding)).empty())     return Result<ThemeData, std::string>::Err(err);
    }

    data.path = path;
    return Result<ThemeData, std::string>::Ok(std::move(data));
}

// ============================================================================
//  Internal – apply to live ImGuiStyle
// ============================================================================

void ThemeManager::ApplyData(const ThemeData& data) {
    ImGuiStyle& style = ImGui::GetStyle();

    // Apply only the colours that were supplied; leave the rest as-is
    // (the caller has already set the base style via StyleColorsDark()).
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        if (data.colorSet[i]) {
            style.Colors[i] = ImVec4(
                data.colors[i][0],
                data.colors[i][1],
                data.colors[i][2],
                data.colors[i][3]);
        }
    }

    // Optional style overrides
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