#include "DevicePanel.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include "Devices/DeviceManager.h"
#include "Preferences/Preferences.h"
#include "Visualizers/FlightStickVisualizer.h"
#include "Visualizers/FlightStickHapticsVisualizer.h"
#include "Visualizers/GamepadHapticsVisualizer.h"
#include "Visualizers/GamepadVisualizer.h"
#include "Visualizers/GenericVisualizer.h"
#include "Visualizers/SteeringWheelHapticsVisualizer.h"
#include "Visualizers/SteeringWheelVisualizer.h"
#include "Visualizers/VirtualDeviceVisualizer.h"
#include "Visualizers/WiimoteVisualizer.h"

#include <string>

// ---------------------------------------------------------------------------
// DrawDeviceVisualizer
// ---------------------------------------------------------------------------

void DrawDeviceVisualizer(const DeviceState&  dev,
                          DeviceManager&      deviceManager,
                          PreferencesManager& prefs)
{
    static GamepadVisualizer              gamepad_viz;
    static GenericVisualizer              generic_viz;
    static SteeringWheelVisualizer        wheel_viz;
    static FlightStickVisualizer          flight_stick_viz;
    static FlightStickHapticsVisualizer   flight_stick_haptics_viz;
    static GamepadHapticsVisualizer       gamepad_haptics_viz;
    static SteeringWheelHapticsVisualizer wheel_haptics_viz;
    static WiimoteVisualizer              wiimote_viz;
    static VirtualDeviceVisualizer        virtual_viz;

    std::string guid         = DeviceManager::GetDeviceGUIDString(dev);
    bool        apply_pref   = !prefs.IsPreferenceApplied(dev.instance_id);
    std::string preferred_viz = prefs.GetVisualizerPreference(guid);

    if (apply_pref)
        prefs.MarkPreferenceApplied(dev.instance_id);

    const bool isVirtual = SDL_IsJoystickVirtual(dev.instance_id);
    if (apply_pref && isVirtual && preferred_viz.empty())
        preferred_viz = "Simulate Inputs";

    auto TabItem = [&](const char* label, DeviceVisualizer& visualizer) {
        ImGuiTabItemFlags flags = 0;
        if (apply_pref && preferred_viz == label)
            flags |= ImGuiTabItemFlags_SetSelected;

        if (ImGui::BeginTabItem(label, nullptr, flags)) {
            visualizer.Draw(dev);
            if (prefs.GetVisualizerPreference(guid) != label) {
                prefs.SetVisualizerPreference(guid, label);
                prefs.Save();
            }
            ImGui::EndTabItem();
        }
    };

    auto SimulateTab = [&]() {
        if (!isVirtual) return;

        const char*       simLabel = "Simulate Inputs";
        ImGuiTabItemFlags flags    = (apply_pref && preferred_viz == simLabel)
                                         ? ImGuiTabItemFlags_SetSelected : 0;

        if (ImGui::BeginTabItem(simLabel, nullptr, flags)) {
            virtual_viz.Draw(dev);
            if (prefs.GetVisualizerPreference(guid) != simLabel) {
                prefs.SetVisualizerPreference(guid, simLabel);
                prefs.Save();
            }
            ImGui::EndTabItem();
        }
    };

    if (dev.is_gamepad) {
        if (ImGui::BeginTabBar("DeviceMode")) {
            TabItem("Raw Inputs", generic_viz);
            if (ImGui::BeginTabItem("Haptic Test")) {
                gamepad_haptics_viz.Draw(dev, deviceManager);
                ImGui::EndTabItem();
            }
            SimulateTab();
            ImGui::EndTabBar();
        }
    } else {
        if (ImGui::BeginTabBar("DeviceMode")) {
            TabItem("Raw Inputs", generic_viz);

            const SDL_JoystickType type = SDL_GetJoystickType(dev.joystick);

            if (type == SDL_JOYSTICK_TYPE_FLIGHT_STICK
                || type == SDL_JOYSTICK_TYPE_THROTTLE) {
                if (ImGui::BeginTabItem("Haptic Test")) {
                    flight_stick_haptics_viz.Draw(dev, deviceManager);
                    ImGui::EndTabItem();
                }
            }
            if (dev.name.find("Nintendo") != std::string::npos
                || dev.name.find("Wiimote") != std::string::npos) {
                TabItem("Wiimote", wiimote_viz);
            }
            if (type == SDL_JOYSTICK_TYPE_WHEEL) {
                if (ImGui::BeginTabItem("Haptic Test")) {
                    wheel_haptics_viz.Draw(dev, deviceManager);
                    ImGui::EndTabItem();
                }
            }

            {
                const bool hasRPM = !deviceManager.GetWheelRPMDevices().empty();
                if (hasRPM || type == SDL_JOYSTICK_TYPE_WHEEL
                    || type == SDL_JOYSTICK_TYPE_UNKNOWN) {
                    if (ImGui::BeginTabItem("RPM LEDs")) {
                        wheel_haptics_viz.DrawLEDs(deviceManager);
                        ImGui::EndTabItem();
                    }
                }
            }

            SimulateTab();
            ImGui::EndTabBar();
        }
    }
}

// ---------------------------------------------------------------------------
// DrawDeviceHideControls  (internal helper)
// ---------------------------------------------------------------------------
// Draws the "Hide from other apps" toggle and the Steam-compat checkbox.
// DeviceState is taken by non-const reference so we can update the flag.

static void DrawDeviceHideControls(DeviceState& dev, DeviceManager& deviceManager)
{
    const bool available = deviceManager.IsHideAvailable();

    if (!available) {
#ifdef _WIN32
        ImGui::TextDisabled("Device hiding unavailable (HidHide driver not installed).");
#elif defined(__linux__)
        ImGui::TextDisabled("Device hiding unavailable.");
#elif defined(__APPLE__)
        ImGui::TextDisabled("Device hiding unavailable.");
#else
        ImGui::TextDisabled("Device hiding not supported on this platform.");
#endif
        return;
    }

    // ── Hide toggle ───────────────────────────────────────────────────────
    bool hidden = dev.hide_from_other_apps;

    // Snapshot BEFORE ImGui::Checkbox can modify `hidden`.
    // PushStyleColor / PopStyleColor must be balanced regardless of whether
    // the user just clicked the checkbox (which toggles the local bool mid-frame).
    const bool pushedColors = hidden;

    // Highlight the checkbox red when the device is currently hidden.
    if (pushedColors) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark,      ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    if (ImGui::Checkbox("Hide from other applications", &hidden)) {
        if (!deviceManager.SetDeviceHidden(dev, hidden)) {
            SDL_Log("DevicePanel: SetDeviceHidden failed for '%s'.", dev.name.c_str());
        }
    }

    // Always pop based on the pre-click snapshot, never on the post-click value.
    if (pushedColors)
        ImGui::PopStyleColor(4);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When enabled, this device is hidden from all other applications.\n"
            "InputBridge can still read it normally.\n"
#ifdef _WIN32
            "Requires the HidHide driver (https://github.com/nefarius/HidHide).\n"
            "The hide persists until you uncheck this box or restart the driver."
#elif defined(__linux__)
            "Uses an exclusive evdev grab (EVIOCGRAB).\n"
            "The hide is released automatically when InputBridge exits."
#elif defined(__APPLE__)
            "Uses IOHIDOptionsTypeSeizeDevice.\n"
            "The hide is released automatically when InputBridge exits."
#endif
        );
    }

    // ── Status label ─────────────────────────────────────────────────────
    if (hidden) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(hidden)");
    }

#ifdef _WIN32
    // ── Steam Input compatibility (Windows / HidHide only) ────────────────
    ImGui::Spacing();
    static bool steamCompat = true; // global preference; persisted separately if needed
    if (ImGui::Checkbox("Keep Steam Input access", &steamCompat)) {
        deviceManager.SetSteamInputCompatible(steamCompat);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When enabled, steam.exe is kept in the HidHide allow-list so that\n"
            "Steam Input (gyro, haptics, etc.) continues to work even while\n"
            "the device is hidden from every other application."
        );
    }
#endif
}

// ---------------------------------------------------------------------------
// DrawDeviceItem
// ---------------------------------------------------------------------------

void DrawDeviceItem(DeviceState&        dev,
                    DeviceManager&      deviceManager,
                    PreferencesManager& prefs)
{
    ImGui::PushID(static_cast<int>(dev.instance_id));

    const std::string label = dev.name
        + " [ID: " + std::to_string(dev.instance_id) + "]"
        + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)")
        + (dev.hide_from_other_apps ? "  [HIDDEN]" : "");

    const bool header_open = ImGui::CollapsingHeader(
        label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    // ── Battery indicator ─────────────────────────────────────────────────
    const bool hasBattery = (dev.battery_state != SDL_POWERSTATE_UNKNOWN
                             || dev.battery_percent >= 0)
                            && dev.battery_state != SDL_POWERSTATE_NO_BATTERY;
    if (hasBattery) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2      rect_min  = ImGui::GetItemRectMin();
        ImVec2      rect_max  = ImGui::GetItemRectMax();

        const float icon_h = ImGui::GetTextLineHeight();
        const float icon_w = icon_h * 1.6f;
        const float pad    = ImGui::GetStyle().FramePadding.x;

        ImVec2 icon_pos = ImVec2(
            rect_max.x - icon_w - pad,
            rect_min.y + (rect_max.y - rect_min.y - icon_h) * 0.5f);

        ImU32 bat_col = ImGui::GetColorU32(ImGuiCol_Text);
        if (dev.battery_state == SDL_POWERSTATE_CHARGING
            || dev.battery_state == SDL_POWERSTATE_CHARGED) {
            bat_col = IM_COL32(50, 255, 50, 255);
        } else if (dev.battery_percent >= 0) {
            if      (dev.battery_percent <= 20) bat_col = IM_COL32(255,  50,  50, 255);
            else if (dev.battery_percent <= 50) bat_col = IM_COL32(255, 200,  50, 255);
            else                                bat_col = IM_COL32( 50, 255,  50, 255);
        }

        const float body_w = icon_w * 0.85f;
        const float term_w = icon_w * 0.15f;
        const float term_h = icon_h * 0.4f;
        (void)term_w;

        draw_list->AddRect(icon_pos, icon_pos + ImVec2(body_w, icon_h),
                           bat_col, 0.0f, 0, 2.0f);
        draw_list->AddRectFilled(
            icon_pos + ImVec2(body_w, (icon_h - term_h) * 0.5f),
            icon_pos + ImVec2(icon_w, (icon_h + term_h) * 0.5f),
            bat_col);

        if (dev.battery_percent >= 0) {
            const float fill_w = (body_w - 4.0f) * (dev.battery_percent / 100.0f);
            if (fill_w > 0.0f) {
                draw_list->AddRectFilled(
                    icon_pos + ImVec2(2.0f, 2.0f),
                    icon_pos + ImVec2(2.0f + fill_w, icon_h - 2.0f),
                    bat_col);
            }
        }

        if (dev.battery_state == SDL_POWERSTATE_CHARGING) {
            const ImVec2 center = icon_pos + ImVec2(body_w * 0.5f, icon_h * 0.5f);
            draw_list->AddLine(center + ImVec2(-3, 0), center + ImVec2(3, 0),
                               IM_COL32(255, 255, 255, 255), 2.0f);
            draw_list->AddLine(center + ImVec2(0, -3), center + ImVec2(0, 3),
                               IM_COL32(255, 255, 255, 255), 2.0f);
        }
    }

    if (header_open) {
        ImGui::Indent();

        // ── Battery detail ────────────────────────────────────────────────
        if (hasBattery) {
            const char* state_str = "Unknown";
            switch (dev.battery_state) {
                case SDL_POWERSTATE_ON_BATTERY: state_str = "On Battery";    break;
                case SDL_POWERSTATE_NO_BATTERY: state_str = "No Battery";    break;
                case SDL_POWERSTATE_CHARGING:   state_str = "Charging";      break;
                case SDL_POWERSTATE_CHARGED:    state_str = "Fully Charged"; break;
                default: break;
            }

            ImGui::Text("Battery: %s", state_str);
            if (dev.battery_percent >= 0) {
                ImGui::SameLine();
                ImGui::Text("(%d%%)", dev.battery_percent);

                const float   battery_fraction = dev.battery_percent / 100.0f;
                ImVec4        bar_color         = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
                if      (dev.battery_percent < 30) bar_color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                else if (dev.battery_percent < 70) bar_color = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
                ImGui::ProgressBar(battery_fraction, ImVec2(-1, 0), "");
                ImGui::PopStyleColor();
            }
        }

        // ── Hide controls ─────────────────────────────────────────────────
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Device Visibility")) {
            ImGui::Indent();
            DrawDeviceHideControls(dev, deviceManager);
            ImGui::Unindent();
        }
        ImGui::Spacing();

        DrawDeviceVisualizer(dev, deviceManager, prefs);
        ImGui::Unindent();
    }

    ImGui::PopID();
}