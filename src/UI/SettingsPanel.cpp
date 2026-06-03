#include "SettingsPanel.h"

#include "UI/FontManager.h"
#include "UI/ThemeManager.h"
#include "Devices/DeviceManager.h"

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
                         bool&               enable_battery_led,
                         bool&               disable_gamepad_nav,
                         bool&               disable_keyboard_nav,
                         DeviceManager&      deviceManager)
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

    int batteryInterval = deviceManager.GetBatteryUpdateInterval();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderInt("Battery Poll Interval (ms)", &batteryInterval, 1000, 60000)) {
        deviceManager.SetBatteryUpdateInterval(batteryInterval);
        prefs.SetInt("BatteryUpdateIntervalMs", batteryInterval);
        prefs.Save();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How often to query battery status from the OS.\nIncreasing this saves CPU but makes indicators laggier.");

    if (ImGui::Checkbox("Disable Gamepad / Steering Wheel UI Navigation", &disable_gamepad_nav)) {
        prefs.SetBool("DisableGamepadNavigation", disable_gamepad_nav);
        prefs.Save();
        ImGuiIO& mio = ImGui::GetIO();
        if (disable_gamepad_nav)
            mio.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        else
            mio.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When enabled, gamepad and steering wheel axes/buttons\n"
            "no longer move focus or activate UI controls.\n"
            "Device data forwarding and haptics are unaffected.");

    if (ImGui::Checkbox("Disable Keyboard UI Navigation", &disable_keyboard_nav)) {
        prefs.SetBool("DisableKeyboardNavigation", disable_keyboard_nav);
        prefs.Save();
        ImGuiIO& mio = ImGui::GetIO();
        if (disable_keyboard_nav)
            mio.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        else
            mio.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When enabled, keyboard arrow keys, Tab, and Enter\n"
            "no longer move focus or activate UI controls.\n"
            "Text input fields are unaffected.");
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

    // Indices: 0 = Default (Dark), 1 = Default (Light), 2+ = file-based themes.
    int comboIndex;
    if (theme.HasCustomTheme())
        comboIndex = theme.GetCurrentEntryIndex() + 2;  // offset past the two built-ins
    else if (theme.IsDefaultLight())
        comboIndex = 1;
    else
        comboIndex = 0;

    std::vector<const char*> comboItems;
    comboItems.reserve(entries.size() + 2);
    comboItems.push_back("Default (Dark)");
    comboItems.push_back("Default (Light)");
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
        } else if (comboIndex == 1) {
            theme.ApplyDefaultLight();
            theme.SaveToPreferences(prefs);
            prefs.Save();
            UpdateUIScale(window, user_ui_scale, user_font_scale,
                          scale_with_window, initial_width, prefs);
        } else {
            const int entryIdx = comboIndex - 2;
            if (entryIdx >= 0 && entryIdx < static_cast<int>(entries.size())) {
                auto result = theme.LoadFromFile(entries[entryIdx].path);
                if (result.IsOk()) {
                    theme.SaveToPreferences(prefs);
                    prefs.Save();
                    UpdateUIScale(window, user_ui_scale, user_font_scale,
                                  scale_with_window, initial_width, prefs);
                }
                comboIndex = theme.HasCustomTheme()
                             ? theme.GetCurrentEntryIndex() + 2 : 0;
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
