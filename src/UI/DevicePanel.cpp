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
    // Visualizer instances are kept alive across frames (ImGui immediate mode
    // pattern: static locals are initialised once and reused every frame).
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

    // Virtual devices default to the Simulate Inputs tab.
    const bool isVirtual = SDL_IsJoystickVirtual(dev.instance_id);
    if (apply_pref && isVirtual && preferred_viz.empty())
        preferred_viz = "Simulate Inputs";

    // Helper: opens a tab item and persists the user's selection.
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

    // Helper: opens the Simulate Inputs tab for virtual devices and persists selection.
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

            if (type == SDL_JOYSTICK_TYPE_WHEEL || type == SDL_JOYSTICK_TYPE_UNKNOWN) {
                // TabItem("Steering Wheel", wheel_viz);
            }
            if (type == SDL_JOYSTICK_TYPE_FLIGHT_STICK
                || type == SDL_JOYSTICK_TYPE_THROTTLE
                || type == SDL_JOYSTICK_TYPE_UNKNOWN) {
                // TabItem("Flight Stick", flight_stick_viz);
            }
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

            // RPM LED tab: shown for any non-gamepad device when wheel-rpm-lib
            // has found at least one supported device, regardless of whether SDL
            // recognises the wheel's haptic interface.
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
// DrawDeviceItem
// ---------------------------------------------------------------------------

void DrawDeviceItem(const DeviceState&  dev,
                    DeviceManager&      deviceManager,
                    PreferencesManager& prefs)
{
    ImGui::PushID(static_cast<int>(dev.instance_id));

    const std::string label = dev.name
        + " [ID: " + std::to_string(dev.instance_id) + "]"
        + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)");

    const bool header_open = ImGui::CollapsingHeader(
        label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    // Battery indicator drawn over the right edge of the collapsing header.
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

        // Choose colour by charge state / level.
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
        (void)term_w; // calculated but only used for terminal nub via AddRectFilled

        // Battery body outline.
        draw_list->AddRect(icon_pos, icon_pos + ImVec2(body_w, icon_h),
                           bat_col, 0.0f, 0, 2.0f);
        // Terminal nub.
        draw_list->AddRectFilled(
            icon_pos + ImVec2(body_w, (icon_h - term_h) * 0.5f),
            icon_pos + ImVec2(icon_w, (icon_h + term_h) * 0.5f),
            bat_col);

        // Fill level.
        if (dev.battery_percent >= 0) {
            const float fill_w = (body_w - 4.0f) * (dev.battery_percent / 100.0f);
            if (fill_w > 0.0f) {
                draw_list->AddRectFilled(
                    icon_pos + ImVec2(2.0f, 2.0f),
                    icon_pos + ImVec2(2.0f + fill_w, icon_h - 2.0f),
                    bat_col);
            }
        }

        // Charging cross / plus symbol.
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

        // Detailed battery information.
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

        DrawDeviceVisualizer(dev, deviceManager, prefs);
        ImGui::Unindent();
    }

    ImGui::PopID();
}
