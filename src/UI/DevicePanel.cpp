#include "App/Log.h"
#include "DevicePanel.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include "Devices/DeviceManager.h"
#include "Preferences/Preferences.h"
#include "UI/DeviceIconProvider.h"
#include "Visualizers/FlightStickVisualizer.h"
#include "Visualizers/FlightStickHapticsVisualizer.h"
#include "Visualizers/GamepadHapticsVisualizer.h"
#include "Visualizers/GamepadVisualizer.h"
#include "Visualizers/GenericVisualizer.h"
#include "Visualizers/SteeringWheelHapticsVisualizer.h"
#include "Visualizers/SteeringWheelVisualizer.h"
#include "Visualizers/VirtualDeviceVisualizer.h"
#include "Visualizers/WiimoteVisualizer.h"
#include "Visualizers/SensorVisualizer.h"
#include <SDL3/SDL.h>
#include <algorithm>

#include <string>

static constexpr const char* kTag = "DevicePanel";

// ---------------------------------------------------------------------------
// DrawDeviceSettingsTab
// ---------------------------------------------------------------------------
// Draws the contents of the per-device "Settings" tab. Currently this just
// hosts the "Infinite-effect keepalive" toggle, which used to live on each
// Haptic Test tab individually.

static void DrawDeviceSettingsTab(const DeviceState&  dev,
                                   DeviceManager&      deviceManager,
                                   PreferencesManager& prefs,
                                   const std::string&  guid)
{
    HapticDevice* haptic = deviceManager.GetHapticDevice(dev.instance_id);

    if (!haptic) {
        ImGui::TextDisabled("No device-specific settings available.");
        return;
    }

    // Restore saved preference once when the device first appears.
    if (!prefs.IsPreferenceApplied(dev.instance_id)) {
        haptic->EnableKeepalive(prefs.GetDeviceKeepalive(guid));
    }

    bool keepalive = haptic->IsKeepaliveEnabled();
    if (ImGui::Checkbox("Infinite-effect keepalive", &keepalive)) {
        deviceManager.SetDeviceKeepalive(dev.instance_id, keepalive);
        prefs.SetDeviceKeepalive(guid, keepalive);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Re-uploads active infinite-duration effects every 30 s.\n"
            "Required for devices that truncate SDL_HAPTIC_INFINITY to ~65 s\n"
            "(e.g. Thrustmaster T150). Safe to enable on any device."
        );
    }
}

// ---------------------------------------------------------------------------
// DrawDeviceVisualizer
// ---------------------------------------------------------------------------

void DrawDeviceVisualizer(const DeviceState&  dev,
                          DeviceManager&      deviceManager,
                          PreferencesManager& prefs,
                          bool                show_named_inputs)
{
    static GamepadVisualizer              gamepad_viz;
    static GenericVisualizer              generic_viz;
    static SteeringWheelVisualizer        wheel_viz;
    static FlightStickVisualizer          flight_stick_viz;
    static FlightStickHapticsVisualizer   flight_stick_haptics_viz;
    static GamepadHapticsVisualizer       gamepad_haptics_viz;
    static SteeringWheelHapticsVisualizer wheel_haptics_viz;
    static WiimoteVisualizer              wiimote_viz;
    static SensorVisualizer               sensor_viz;
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

    // Raw Inputs tab uses the generic visualizer which accepts the named-inputs flag.
    auto RawInputsTab = [&]() {
        const char*       label = "Raw Inputs";
        ImGuiTabItemFlags flags = (apply_pref && preferred_viz == label)
                                      ? ImGuiTabItemFlags_SetSelected : 0;

        if (ImGui::BeginTabItem(label, nullptr, flags)) {
            generic_viz.Draw(dev, show_named_inputs);
            if (prefs.GetVisualizerPreference(guid) != label) {
                prefs.SetVisualizerPreference(guid, label);
                prefs.Save();
            }
            ImGui::EndTabItem();
        }
    };

    if (dev.is_gamepad) {
        if (ImGui::BeginTabBar("DeviceMode")) {
            RawInputsTab();
            if (ImGui::BeginTabItem("Haptic Test")) {
                gamepad_haptics_viz.Draw(dev, deviceManager, prefs, guid);
                ImGui::EndTabItem();
            }

            // Show the Sensors tab only for controllers that have a gyro,
            // accelerometer, or touchpad (DualSense, Steam Controller, etc.).
            // We check via SDL rather than casting HapticDevice to GamepadHaptics
            // so the tab still appears when the haptic system fails to initialise.
            if (dev.gamepad) {
                bool hasSensors = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO)
                               || SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL)
                               || SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_L)
                               || SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_R)
                               || SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_L)
                               || SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_R)
                               || SDL_GetNumGamepadTouchpads(dev.gamepad) > 0;
                if (hasSensors)
                    TabItem("Sensors", sensor_viz);
            }

            if (ImGui::BeginTabItem("Settings")) {
                DrawDeviceSettingsTab(dev, deviceManager, prefs, guid);
                ImGui::EndTabItem();
            }

            SimulateTab();
            ImGui::EndTabBar();
        }
    } else {
        if (ImGui::BeginTabBar("DeviceMode")) {
            RawInputsTab();

            const SDL_JoystickType type = SDL_GetJoystickType(dev.joystick);

            if (type == SDL_JOYSTICK_TYPE_FLIGHT_STICK
                || type == SDL_JOYSTICK_TYPE_THROTTLE) {
                if (ImGui::BeginTabItem("Haptic Test")) {
                    flight_stick_haptics_viz.Draw(dev, deviceManager, prefs, guid);
                    ImGui::EndTabItem();
                }
            }
            if (dev.name.find("Nintendo") != std::string::npos
                || dev.name.find("Wiimote") != std::string::npos) {
                TabItem("Wiimote", wiimote_viz);
            }
            if (type == SDL_JOYSTICK_TYPE_WHEEL) {
                if (ImGui::BeginTabItem("Haptic Test")) {
                    wheel_haptics_viz.Draw(dev, deviceManager, prefs, guid);
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

            if (ImGui::BeginTabItem("Settings")) {
                DrawDeviceSettingsTab(dev, deviceManager, prefs, guid);
                ImGui::EndTabItem();
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
            LOG_WARN(kTag, "SetDeviceHidden failed for '%s'.", dev.name.c_str());
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
// GetBatteryColor
// ---------------------------------------------------------------------------
// Returns a colour for a battery given its state and percentage.
static ImU32 GetBatteryColorFor(SDL_PowerState state, int percent)
{
    if (state == SDL_POWERSTATE_CHARGING || state == SDL_POWERSTATE_CHARGED)
        return IM_COL32(50, 255, 50, 255);
    if (percent >= 0) {
        if      (percent <= 20) return IM_COL32(255,  50,  50, 255);
        else if (percent <= 50) return IM_COL32(255, 200,  50, 255);
        else                    return IM_COL32( 50, 255,  50, 255);
    }
    return ImGui::GetColorU32(ImGuiCol_Text);
}

static ImU32 GetBatteryColor(const DeviceState& dev)
{
    return GetBatteryColorFor(dev.battery_state, dev.battery_percent);
}

// ---------------------------------------------------------------------------
// DrawDeviceItem
// ---------------------------------------------------------------------------

void DrawDeviceItem(DeviceState&        dev,
                    DeviceManager&      deviceManager,
                    PreferencesManager& prefs,
                    bool                show_named_inputs)
{
    ImGui::PushID(static_cast<int>(dev.instance_id));

    // ── Device icon ───────────────────────────────────────────────────────
    // Resolve the Kenney icon for this device before building the label so we
    // know whether to reserve leading space for it.
    const DeviceIcon icon = DeviceIconProvider::GetIcon(dev);
    const float      font_sz  = ImGui::GetFontSize();

    // When an icon is available we pad the label with a leading space wide
    // enough to accommodate the glyph.  ImGui CollapsingHeader renders the
    // text starting just after the tree arrow; we overlay the glyph there.
    std::string label;
    if (icon.IsValid()) {
        // Reserve space: "\t" would collapse, so we use a fixed number of
        // spaces.  We'll draw the actual glyph via ImDrawList afterwards.
        label  = "      ";   // ~row-height icon width at default font sizes
    }
    label += dev.name
        + " [ID: " + std::to_string(dev.instance_id) + "]"
        + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)")
        + (dev.hide_from_other_apps ? "  [HIDDEN]" : "");

    const bool header_open = ImGui::CollapsingHeader(
        label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    // Overlay the Kenney icon glyph at the left of the header row.
    if (icon.IsValid()) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2      rect_min  = ImGui::GetItemRectMin();
        ImVec2      rect_max  = ImGui::GetItemRectMax();

        const float row_h   = rect_max.y - rect_min.y;
        const float pad_x   = ImGui::GetStyle().FramePadding.x;
        const float arrow_w = font_sz; // ImGui tree arrow occupies ~1 em

        // Kenney pictogram glyphs occupy only ~30-50% of their em square,
        // with a large gap above (Y0) and below (Size-Y1).
        // fill the row height minus a small inset, then position
        // them precisely centred in the row.
        const float unscaled_sz  = ImGui::GetStyle().FontSizeBase;
        const float kenney_baked = unscaled_sz * 4.0f;
        const float scaled_sz   = ImGui::GetFontSize();
        const float target_h    = row_h - ImGui::GetStyle().FramePadding.y * 2.0f;

        float render_sz      = scaled_sz; // fallback
        float y0_scaled      = 0.0f;    // top gap inside em square at render_sz
        float glyph_h_scaled = scaled_sz; // visible pixel height at render_sz
        if (ImFontBaked* baked = icon.font->GetFontBaked(kenney_baked))
        {
            const ImFontGlyph* g = baked->FindGlyphNoFallback(icon.codepoint);
            if (g && baked->Size > 0.0f)
            {
                const float glyph_h = g->Y1 - g->Y0;
                const float fill    = glyph_h / baked->Size;
                if (fill > 0.01f)
                {
                    render_sz = std::min(target_h / fill, row_h); // never exceed row
                    y0_scaled = (g->Y0 / baked->Size) * render_sz;
                    // Store exact scaled glyph height for centring below.
                    // (render_sz - y0_scaled would also include the descender
                    // gap below Y1, pushing the icon upward — use exact value.)
                    glyph_h_scaled = (glyph_h / baked->Size) * render_sz;
                }
            }
        }

        // Horizontally: just after the tree-node arrow.
        const float glyph_x = rect_min.x + pad_x + arrow_w + 2.0f;

        // Vertically: centre the VISIBLE pixels (not the em square) in the row.
        //   row centre           = rect_min.y + row_h * 0.5
        //   visible pixel centre = glyph_y + y0_scaled + glyph_h_scaled * 0.5
        //   => glyph_y (top of em square) = row centre - y0_scaled - glyph_h_scaled/2
        const float glyph_y = rect_min.y
                              + (row_h * 0.5f)
                              - y0_scaled
                              - glyph_h_scaled * 0.5f;

        draw_list->AddText(icon.font, render_sz,
                           ImVec2(glyph_x, glyph_y),
                           ImGui::GetColorU32(ImGuiCol_Text),
                           icon.glyph);
    }

    // ── Battery indicator ─────────────────────────────────────────────────
    const bool hasBattery = (dev.battery_state != SDL_POWERSTATE_UNKNOWN
                             || dev.battery_percent >= 0)
                            && dev.battery_state != SDL_POWERSTATE_NO_BATTERY;
    const bool hasSplitBattery = hasBattery
                                 && dev.battery_state_L != SDL_POWERSTATE_UNKNOWN;
    if (hasBattery) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2      rect_min  = ImGui::GetItemRectMin();
        ImVec2      rect_max  = ImGui::GetItemRectMax();

        const float icon_h = ImGui::GetTextLineHeight();
        const float icon_w = icon_h * 1.6f;
        const float pad    = ImGui::GetStyle().FramePadding.x;
        const float gap    = 4.0f; // gap between L and R icons when split

        // For a split pair we draw two icons; otherwise one.
        const float total_w = hasSplitBattery ? (icon_w * 2.0f + gap) : icon_w;

        ImVec2 icon_pos = ImVec2(
            rect_max.x - total_w - pad,
            rect_min.y + (rect_max.y - rect_min.y - icon_h) * 0.5f);

        // Helper lambda — draws one battery icon at `pos`.
        auto DrawBatteryIcon = [&](ImVec2 pos, SDL_PowerState state, int pct) {
            ImU32 col = GetBatteryColorFor(state, pct);

            const float body_w = icon_w * 0.85f;
            const float term_h = icon_h * 0.4f;

            draw_list->AddRect(pos, pos + ImVec2(body_w, icon_h), col, 0.0f, 0, 2.0f);
            draw_list->AddRectFilled(
                pos + ImVec2(body_w, (icon_h - term_h) * 0.5f),
                pos + ImVec2(icon_w, (icon_h + term_h) * 0.5f),
                col);

            if (pct >= 0) {
                const float fill_w = (body_w - 4.0f) * (pct / 100.0f);
                if (fill_w > 0.0f) {
                    draw_list->AddRectFilled(
                        pos + ImVec2(2.0f, 2.0f),
                        pos + ImVec2(2.0f + fill_w, icon_h - 2.0f),
                        col);
                }
            }

            if (state == SDL_POWERSTATE_CHARGING) {
                const ImVec2 center = pos + ImVec2(body_w * 0.5f, icon_h * 0.5f);
                draw_list->AddLine(center + ImVec2(-3, 0), center + ImVec2(3, 0),
                                   IM_COL32(255, 255, 255, 255), 2.0f);
                draw_list->AddLine(center + ImVec2(0, -3), center + ImVec2(0, 3),
                                   IM_COL32(255, 255, 255, 255), 2.0f);
            }
        };

        if (hasSplitBattery) {
            // Left Joy-Con icon first (leftmost), then Right Joy-Con icon.
            DrawBatteryIcon(icon_pos,
                            dev.battery_state_L, dev.battery_percent_L);
            DrawBatteryIcon(icon_pos + ImVec2(icon_w + gap, 0.0f),
                            dev.battery_state,   dev.battery_percent);
        } else {
            DrawBatteryIcon(icon_pos, dev.battery_state, dev.battery_percent);
        }
    }

    if (header_open) {
        ImGui::Indent();

        // ── Battery detail ────────────────────────────────────────────────
        if (hasBattery) {
            auto DrawBatteryRow = [&](const char* label,
                                      SDL_PowerState state, int pct) {
                const char* state_str = "Unknown";
                switch (state) {
                    case SDL_POWERSTATE_ON_BATTERY: state_str = "On Battery";    break;
                    case SDL_POWERSTATE_NO_BATTERY: state_str = "No Battery";    break;
                    case SDL_POWERSTATE_CHARGING:   state_str = "Charging";      break;
                    case SDL_POWERSTATE_CHARGED:    state_str = "Fully Charged"; break;
                    default: break;
                }
                ImGui::Text("%s %s", label, state_str);
                if (pct >= 0) {
                    ImGui::SameLine();
                    ImGui::Text("(%d%%)", pct);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                          GetBatteryColorFor(state, pct));
                    ImGui::ProgressBar(pct / 100.0f, ImVec2(-1, 0), "");
                    ImGui::PopStyleColor();
                }
            };

            if (hasSplitBattery) {
                DrawBatteryRow("Battery L:", dev.battery_state_L, dev.battery_percent_L);
                DrawBatteryRow("Battery R:", dev.battery_state,   dev.battery_percent);
            } else {
                DrawBatteryRow("Battery:", dev.battery_state, dev.battery_percent);
            }
        }

        // ── Hide controls ─────────────────────────────────────────────────
        /* TODO: fix in future update
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Device Visibility")) {
            ImGui::Indent();
            DrawDeviceHideControls(dev, deviceManager);
            ImGui::Unindent();
        }
        ImGui::Spacing();
        */

        DrawDeviceVisualizer(dev, deviceManager, prefs, show_named_inputs);
        ImGui::Unindent();
    }

    ImGui::PopID();
}