#include "SettingsPanel.h"

#include "UI/EditableSlider.h"
#include "UI/FontManager.h"
#include "UI/IconsFontAwesome6.h"
#include "UI/ThemeManager.h"
#include "Devices/DeviceManager.h"

#include "Mappers/InputMapping/MappingProfileStore.h"
#include "Protocols/ProtocolEditorWindow.h"
#include "Protocols/ProtocolRegistry.h"
#include "Utils/OpenFolder.h"
#include "Utils/XdgDirs.h"

#include <filesystem>
#include <iterator>
#include <vector>

#if defined(__linux__)
#include "Devices/Wiimote/Linux/LinuxUdevInstaller.h"
#include "Devices/Wiimote/Linux/WiimoteLinuxDiagnostics.h"
#include <chrono>
#include <future>
#endif

#if defined(__linux__)
namespace {

// Backs the "Install"/"Remove" buttons in the Linux Permissions section
// below. Same async/poll shape as SidebarLayout.cpp's udev-fix banner
// (pkexec blocks on the user interacting with the auth dialog, so the
// call runs on a background thread and this is polled once per frame),
// but kept as its own copy here since install and remove are independent,
// user-initiated actions rather than one error-recovery flow, and each
// needs its own in-flight/result state.
struct UdevJobUiState {
    std::future<InputBridge::Wiimote::LinuxUdevInstaller::RunOutcome> pending;
    bool has_result = false;
    // True once the current last_result has been shown in the modal at
    // least once - starts true (nothing to show yet), flips false the
    // frame Poll() picks up a fresh result, and back to true as soon as
    // DrawUdevResultModal() has opened the popup for it. This is what
    // lets the modal detect "there's a new result to pop up for" without
    // reopening every frame the popup happens to already be open.
    bool modal_shown = true;
    InputBridge::Wiimote::LinuxUdevInstaller::RunOutcome last_result;

    bool IsRunning() const {
        return pending.valid() &&
               pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }

    void Poll() {
        if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            last_result = pending.get();
            has_result = true;
            modal_shown = false;
        }
    }
};
UdevJobUiState s_UdevInstallJob;
UdevJobUiState s_UdevRemoveJob;

// Renders the outcome of a finished install/remove run. Success shows the
// script's own stdout verbatim - both install and --uninstall end with a
// "Next steps" / "Done." block, and that's the only place those steps are
// written down, so showing anything else here would just be a paraphrase
// the script itself already wrote correctly. Failure shows the reason and
// a stderr tail, same wording as the Devices-tab permission banner.
void DrawUdevJobResult(const UdevJobUiState& job) {
    using Result = InputBridge::Wiimote::LinuxUdevInstaller::Result;
    if (!job.has_result || job.IsRunning()) return;

    if (job.last_result.result == Result::Success) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), ICON_FA_CHECK " Done.");
        if (!job.last_result.stdout_tail.empty())
            ImGui::TextWrapped("%s", job.last_result.stdout_tail.c_str());
    } else {
        const char* why =
            job.last_result.result == Result::UserCancelled  ? "Authentication was cancelled." :
            job.last_result.result == Result::ScriptNotFound ? "install-udev-rules.sh wasn't found "
                                                                 "next to the InputBridge binary." :
            job.last_result.result == Result::PkexecNotFound ? "pkexec isn't available on this system." :
                                                                 "The script failed.";
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", why);
        if (!job.last_result.stderr_tail.empty() && job.last_result.result == Result::Failed)
            ImGui::TextWrapped("%s", job.last_result.stderr_tail.c_str());
    }
}

// Same ESC-to-close convenience used by the other BeginPopupModal blocks
// in this codebase (e.g. ProtocolEditorWindow.cpp) - kept as its own copy
// here since it's a small file-local static, not worth sharing a header for.
void CloseUdevResultModalOnEscape() {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
}

// Shows the outcome of the last Install/Remove run in a modal instead of
// as text sitting under the buttons - same reasoning as the diagnostics
// modal below: the stdout/stderr tail can run several lines, and a modal
// gives it an explicit dismissal instead of permanently taking up space
// in the settings panel after the first run.
//
// One shared popup serves both jobs since only one can run at a time
// (the buttons are mutually disabled via any_running below); each job's
// own modal_shown flag independently tracks whether *it* has a fresh,
// not-yet-shown result, so a completed Install doesn't get overwritten
// or skipped if Remove finishes moments later.
void DrawUdevResultModal() {
    static const UdevJobUiState* s_ResultToShow = nullptr;

    if (!s_UdevInstallJob.modal_shown) {
        s_ResultToShow = &s_UdevInstallJob;
        s_UdevInstallJob.modal_shown = true;
        ImGui::OpenPopup("Permission Rule##udev_modal");
    } else if (!s_UdevRemoveJob.modal_shown) {
        s_ResultToShow = &s_UdevRemoveJob;
        s_UdevRemoveJob.modal_shown = true;
        ImGui::OpenPopup("Permission Rule##udev_modal");
    }

    bool open = true;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 280), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 180), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Permission Rule##udev_modal", &open)) {
        CloseUdevResultModalOnEscape();

        const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
        ImGui::BeginChild("##udev_result", ImVec2(0, -footerH), false);
        if (s_ResultToShow) DrawUdevJobResult(*s_ResultToShow);
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// Results of the last "Check for common issues" run, and whether the modal
// showing them still needs to be opened this frame (set on button click,
// consumed the next time DrawDiagnosticsModal runs). The results persist
// across frames/re-opens rather than being cleared when the modal is
// closed, so re-opening without re-running still shows the last run.
std::vector<InputBridge::Wiimote::WiimoteLinuxDiagnostics::CheckResult> s_DiagnosticsResults;
bool s_ShouldOpenDiagnosticsModal = false;

// Same ESC-to-close convenience used by the other BeginPopupModal blocks
// in this codebase (e.g. ProtocolEditorWindow.cpp) - kept as its own copy
// here since it's a small file-local static, not worth sharing a header for.
void CloseDiagnosticsModalOnEscape() {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
}

void DrawDiagnosticsResult(const InputBridge::Wiimote::WiimoteLinuxDiagnostics::CheckResult& r) {
    using Status = InputBridge::Wiimote::WiimoteLinuxDiagnostics::Status;
    const ImVec4 color = r.status == Status::Ok      ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f) :
                          r.status == Status::Warning ? ImVec4(1.0f, 0.75f, 0.3f, 1.0f) :
                                                         ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    const char* icon = r.status == Status::Ok      ? ICON_FA_CHECK :
                        r.status == Status::Warning ? "!" : "i";
    ImGui::TextColored(color, "%s %s", icon, r.title.c_str());
    ImGui::TextWrapped("%s", r.detail.c_str());
}

// Shows the results of the last "Check for common issues" run in a modal
// dialog rather than inline in the settings panel - the list can get long
// enough (permissions, group membership, tooling, Bluetooth, Steam IR
// conflict) that leaving it inline pushed the rest of the panel down every
// time it was run, and a modal gives it an explicit "done reading this"
// dismissal instead.
void DrawDiagnosticsModal() {
    if (s_ShouldOpenDiagnosticsModal) {
        ImGui::OpenPopup("Common Issues Check##modal");
        s_ShouldOpenDiagnosticsModal = false;
    }

    bool open = true;

    // Auto-center on the main viewport each time the popup opens (not
    // ImGuiCond_Always, so the user's own drag/resize afterwards sticks
    // instead of snapping back to center every frame).
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    // No ImGuiWindowFlags_AlwaysAutoResize/NoResize here (unlike the other
    // modals in this codebase) - the result list's length varies with how
    // many checks warn, so letting the user drag it larger is more useful
    // than forcing an exact-content-fit size every time.
    ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 200), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Common Issues Check##modal", &open)) {
        CloseDiagnosticsModalOnEscape();

        // Results scroll in their own region so Re-check/Close stay pinned
        // at the bottom regardless of how the user resizes the window.
        // Reserve room for the Separator (which adds its own ItemSpacing.y
        // above and below) plus the button row below it, so Re-check/Close
        // stay fully on-screen - and outside any scrolling region - however
        // small the user resizes the window, rather than being clipped or
        // needing a scroll to reach.
        const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
        ImGui::BeginChild("##diag_results", ImVec2(0, -footerH), false);
        if (s_DiagnosticsResults.empty()) {
            ImGui::TextDisabled("No results yet.");
        } else {
            for (size_t i = 0; i < s_DiagnosticsResults.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                DrawDiagnosticsResult(s_DiagnosticsResults[i]);
                ImGui::PopID();
                if (i + 1 < s_DiagnosticsResults.size()) ImGui::Spacing();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        if (ImGui::Button("Re-check", ImVec2(120, 0))) {
            s_DiagnosticsResults = InputBridge::Wiimote::WiimoteLinuxDiagnostics::RunAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

} // namespace
#endif // __linux__

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
                         DeviceManager&      deviceManager,
                         bool&               show_named_inputs,
                         bool&               show_slider_edit_buttons)
{
    // -- Performance --------------------------------------------------------
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

    // -- Device Settings ----------------------------------------------------
    if (ImGui::Checkbox("Battery LED Indicator", &enable_battery_led)) {
        prefs.SetBool("EnableBatteryLED", enable_battery_led);
        prefs.Save();
    }

    int batteryInterval = deviceManager.GetBatteryUpdateInterval();
    ImGui::SetNextItemWidth(200.0f);
    if (UI::SliderInt("Battery Poll Interval (ms)", &batteryInterval, 1000, 60000, "%d",
                       "How often to query battery status from the OS.\nIncreasing this saves CPU but makes indicators laggier.")) {
        deviceManager.SetBatteryUpdateInterval(batteryInterval);
        prefs.SetInt("BatteryUpdateIntervalMs", batteryInterval);
        prefs.Save();
    }

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

    if (ImGui::Checkbox("Named Inputs", &show_named_inputs)) {
        prefs.SetBool("ShowNamedInputs", show_named_inputs);
        prefs.Save();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Show known input names (Left Stick X, A, D-Pad Up, \xe2\x80\xa6)\n"
            "with matching controller icons where available.\n"
            "Applies to the Raw Inputs tab for devices recognised as gamepads by SDL.");

    if (ImGui::Checkbox("Slider Edit Buttons", &show_slider_edit_buttons)) {
        prefs.SetBool("ShowSliderEditButtons", show_slider_edit_buttons);
        prefs.Save();
        UI::SetSliderEditButtonsEnabled(show_slider_edit_buttons);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Show a pen button next to sliders for typing an exact value.\n"
            "Sliders can always be edited this way via Ctrl+Click as well.");
    ImGui::Separator();

    // -- UI Scale controls --------------------------------------------------
    bool changed       = false;
    bool scale_changed = false;

    // Helper: only SameLine if the next item (of estimated width nextW) fits.
    // Falls through to a new line otherwise, so narrow sidebars wrap cleanly.
    auto SameLineIfFits = [](float nextW) {
        float spaceLeft = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x;
        if (nextW <= spaceLeft) ImGui::SameLine();
    };

    const float btnW      = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f; // approx square btn
    const float textWUI   = ImGui::CalcTextSize("UI Scale: 3.00").x;
    const float textWFont = ImGui::CalcTextSize("Font Scale: 3.00").x;
    const float resetW    = ImGui::CalcTextSize("Reset UI").x + ImGui::GetStyle().FramePadding.x * 2.0f;

    if (ImGui::Button("-##UI")) {
        user_ui_scale -= 0.1f;
        if (user_ui_scale < 0.5f) user_ui_scale = 0.5f;
        scale_changed = true;
    }
    SameLineIfFits(btnW);
    if (ImGui::Button("+##UI")) {
        user_ui_scale += 0.1f;
        if (user_ui_scale > 3.0f) user_ui_scale = 3.0f;
        scale_changed = true;
    }
    SameLineIfFits(textWUI);
    ImGui::Text("UI Scale: %.2f", user_ui_scale);

    // -- Font Scale controls ------------------------------------------------
    bool font_scale_changed = false;

    if (ImGui::Button("-##Font")) {
        user_font_scale -= 0.1f;
        if (user_font_scale < 0.5f) user_font_scale = 0.5f;
        font_scale_changed = true;
    }
    SameLineIfFits(btnW);
    if (ImGui::Button("+##Font")) {
        user_font_scale += 0.1f;
        if (user_font_scale > 3.0f) user_font_scale = 3.0f;
        font_scale_changed = true;
    }
    SameLineIfFits(textWFont);
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

    // -- Colour Theme dropdown ----------------------------------------------
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
            "No themes found - place .json files in the themes/ folder");

    if (!theme.GetLastError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("Error: %s", theme.GetLastError().c_str());
        ImGui::PopStyleColor();
    }

    
    // -- External Folders ---------------------------------------------------
    // One button per folder InputBridge reads/writes outside of itself, so
    // users can find, back up, or hand-edit files without hunting through
    // platform-specific config/data locations.
    ImGui::Separator();
    ImGui::Text("External Folders");

    struct FolderEntry {
        const char* label;
        std::string path;
        const char* tooltip;
    };
    const FolderEntry folders[] = {
        {"Config",           XdgDirs::configDir(),
         "Preferences and window layout (preferences.ini, imgui.ini)"},
        {"Mapping Profiles", InputMapping::MappingProfileStore::GetMappingsDirectory().string(),
         "Saved input-mapping profiles (.json)"},
        {"Protocols",        ProtocolRegistry::GetProtocolsDir(),
         "Protocol definitions and field-catalog templates"},
        {"Themes",           ThemeManager::GetInstance().GetThemesDir(),
         "Colour theme files (.json) - drop new ones here, then hit Refresh above"},
        {"Fonts",            GetFontsDir(),
         "Bundled fonts, including the Font Awesome icon set"},
        {"Backups",          ProtocolEditorWindow::GetBackupDir(),
         "Automatic backups made before destructive protocol-editor operations"},
    };

    // Cleared on every successful open; persists across frames otherwise so
    // the message stays visible until the user tries again.
    static std::string s_openFolderError;

    for (size_t i = 0; i < std::size(folders); ++i) {
        const FolderEntry& f = folders[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button(f.label)) {
            std::string err;
            if (OpenFolderInFileBrowser(f.path, &err)) s_openFolderError.clear();
            else s_openFolderError = err;
        }
        ImGui::PopID();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n\n%s", f.tooltip, f.path.empty() ? "(unavailable)" : f.path.c_str());

        // Wrap to the next line instead of running off the edge of a narrow
        // sidebar: only continue the row if the *next* button would still
        // fit in the space remaining.
        if (i + 1 < std::size(folders)) {
            float nextW = ImGui::CalcTextSize(folders[i + 1].label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float spaceLeft = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x;
            if (nextW <= spaceLeft) ImGui::SameLine();
        }
    }

    if (!s_openFolderError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("Error: %s", s_openFolderError.c_str());
        ImGui::PopStyleColor();
    }

#if defined(__linux__)
    // -- Linux Permissions ---------------------------------------------
    // The Devices tab only ever offers to *install* the udev rule, and
    // only reactively when a scan just hit a permission error. Removing
    // it again isn't tied to any error state, so it needs a home that's
    // available regardless - here, alongside the app's other persistent
    // maintenance actions.
    {
        using InputBridge::Wiimote::LinuxUdevInstaller;

        ImGui::Separator();
        ImGui::Text("Linux Permissions");
        ImGui::TextWrapped(
            "Controls the udev rule that lets InputBridge open a Wii Remote / "
            "Balance Board's hidraw device without root - needed when it's "
            "connected through a USB Bluetooth dongle. Both actions prompt "
            "for authentication via pkexec.");

        s_UdevInstallJob.Poll();
        s_UdevRemoveJob.Poll();

        const bool pkexec_available = LinuxUdevInstaller::IsPkexecAvailable();
        const bool any_running = s_UdevInstallJob.IsRunning() || s_UdevRemoveJob.IsRunning();

        ImGui::BeginDisabled(!pkexec_available || any_running);
        if (ImGui::Button("Install permission rule")) {
            s_UdevInstallJob.has_result = false;
            s_UdevInstallJob.pending = std::async(std::launch::async, &LinuxUdevInstaller::InstallRules);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove permission rule")) {
            s_UdevRemoveJob.has_result = false;
            s_UdevRemoveJob.pending = std::async(std::launch::async, &LinuxUdevInstaller::UninstallRules);
        }
        ImGui::EndDisabled();

        if (!pkexec_available) {
            ImGui::TextDisabled("pkexec not found - install it, or run "
                                 "packaging/linux/install-udev-rules.sh manually as root.");
        } else if (any_running) {
            ImGui::TextDisabled("Waiting for authentication...");
        }

        DrawUdevResultModal();

        // -- Diagnostics --------------------------------------------------
        // Separate from Install/Remove above: those two change system
        // state and need pkexec, this only ever reads things, so it stays
        // enabled and usable even when pkexec isn't available.
        ImGui::Spacing();
        if (ImGui::Button("Check for common issues")) {
            s_DiagnosticsResults = InputBridge::Wiimote::WiimoteLinuxDiagnostics::RunAll();
            s_ShouldOpenDiagnosticsModal = true;
        }
        DrawDiagnosticsModal();
    }
#endif // __linux__
}
