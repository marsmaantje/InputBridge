#include "SettingsPanel.h"

#include "UI/FontManager.h"
#include "UI/ThemeManager.h"

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
                         bool&               enable_battery_led)
{
    // ── Performance ────────────────────────────────────────────────────────
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / io.Framerate, io.Framerate);

    if (ImGui::Checkbox("VSync", &vsync))
        SDL_SetRenderVSync(renderer, vsync ? 1 : 0);

    ImGui::SameLine();
    {
        const char* fl = "Framerate Limit";
        float fw = ImGui::GetContentRegionAvail().x
                   - ImGui::CalcTextSize(fl).x
                   - ImGui::GetStyle().ItemInnerSpacing.x;
        if (fw < 10.0f) fw = 10.0f;
        ImGui::SetNextItemWidth(fw);
        ImGui::InputInt(fl, &framerate_limit);
        if (framerate_limit < 0) framerate_limit = 0;
    }
    ImGui::Separator();

    // ── Device Settings ────────────────────────────────────────────────────
    if (ImGui::Checkbox("Battery LED Indicator", &enable_battery_led)) {
        prefs.SetBool("EnableBatteryLED", enable_battery_led);
        prefs.Save();
    }
    ImGui::Separator();

    // ── UI Scale controls ──────────────────────────────────────────────────
    bool changed       = false;
    bool scale_changed = false;

    if (ImGui::Button("-##UI")) {
        user_ui_scale -= 0.1f;
        if (user_ui_scale < 0.5f) user_ui_scale = 0.5f;
        scale_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+##UI")) {
        user_ui_scale += 0.1f;
        if (user_ui_scale > 3.0f) user_ui_scale = 3.0f;
        scale_changed = true;
    }
    ImGui::SameLine();
    ImGui::Text("UI Scale: %.2f", user_ui_scale);

    // ── Font Scale controls ────────────────────────────────────────────────
    bool font_scale_changed = false;

    if (ImGui::Button("-##Font")) {
        user_font_scale -= 0.1f;
        if (user_font_scale < 0.5f) user_font_scale = 0.5f;
        font_scale_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+##Font")) {
        user_font_scale += 0.1f;
        if (user_font_scale > 3.0f) user_font_scale = 3.0f;
        font_scale_changed = true;
    }
    ImGui::SameLine();
    ImGui::Text("Font Scale: %.2f", user_font_scale);

    if (scale_changed) {
        scale_with_window = false;
        changed = true;
    }
    if (ImGui::Checkbox("Scale with Window", &scale_with_window))
        changed = true;

    if (ImGui::Button("Reset UI")) {
        user_ui_scale     = 1.3f;
        user_font_scale   = 1.0f;
        scale_with_window = false;
        SDL_SetWindowSize(window, initial_width, initial_height);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        changed = true;
    }

    if (changed || font_scale_changed) {
        prefs.SetFloat("UIScale",          user_ui_scale);
        prefs.SetFloat("FontScale",        user_font_scale);
        prefs.SetBool("ScaleWithWindow",   scale_with_window);
        prefs.Save();
        UpdateUIScale(window, user_ui_scale, user_font_scale,
                      scale_with_window, initial_width, prefs);
    }

    // ── Colour Theme dropdown ──────────────────────────────────────────────
    ImGui::Separator();
    ImGui::Text("Colour Theme");

    ThemeManager& theme   = ThemeManager::GetInstance();
    const auto&   entries = theme.GetAvailableThemes();

    int comboIndex = theme.HasCustomTheme() ? theme.GetCurrentEntryIndex() + 1 : 0;

    std::vector<const char*> comboItems;
    comboItems.reserve(entries.size() + 1);
    comboItems.push_back("Default (Dark)");
    for (const auto& e : entries)
        comboItems.push_back(e.displayName.c_str());

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("##ThemeCombo", &comboIndex,
                     comboItems.data(), static_cast<int>(comboItems.size()))) {
        if (comboIndex == 0) {
            theme.ApplyDefault();
            theme.SaveToPreferences(prefs);
            prefs.Save();
            UpdateUIScale(window, user_ui_scale, user_font_scale,
                          scale_with_window, initial_width, prefs);
        } else {
            const int entryIdx = comboIndex - 1;
            if (entryIdx >= 0 && entryIdx < static_cast<int>(entries.size())) {
                auto result = theme.LoadFromFile(entries[entryIdx].path);
                if (result.IsOk()) {
                    theme.SaveToPreferences(prefs);
                    prefs.Save();
                    UpdateUIScale(window, user_ui_scale, user_font_scale,
                                  scale_with_window, initial_width, prefs);
                }
                comboIndex = theme.HasCustomTheme()
                             ? theme.GetCurrentEntryIndex() + 1 : 0;
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        theme.Refresh();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rescan the themes/ folder for new .json files");

    if (entries.empty())
        ImGui::TextDisabled(
            "No themes found — place .json files in the themes/ folder");

    if (!theme.GetLastError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("Error: %s", theme.GetLastError().c_str());
        ImGui::PopStyleColor();
    }
}
