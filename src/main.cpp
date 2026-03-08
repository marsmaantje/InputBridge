#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_internal.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <SDL3/SDL_filesystem.h>

#include "Devices/DeviceManager.h"
#include "Devices/DeviceState.h"
#include "Mappers/InputMapper.h"
#include "Mappers/OutputMapper.h"
#include "Network/NetworkStatusWindow.h"
#include "Preferences/Preferences.h"
#include "UI/ThemeManager.h"
#include "Protocols/OSCProtocol.h"
#include "Protocols/ProtocolEditorWindow.h"
#include "Network/OSCServer.h"
#include "Network/WebSocketServer.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#if ENABLE_WEBSOCKETS
#include "Protocols/WebSocketProtocol.h"
#endif
#include "Visualizers/FlightStickVisualizer.h"
#include "Visualizers/GamepadVisualizer.h"
#include "Visualizers/GenericVisualizer.h"
#include "Visualizers/SteeringWheelVisualizer.h"
#include "Visualizers/GamepadHapticsVisualizer.h"
#include "Visualizers/WiimoteVisualizer.h"
#include "Visualizers/SteeringWheelHapticsVisualizer.h"

// Note: For SDL3, we may need to link against SDL3_net if we want use it.
// #include <SDL3_net/SDL_net.h>

void UpdateUIScale(SDL_Window *window, float& user_ui_scale, float& user_font_scale, bool scale_with_window, int initial_width, PreferencesManager& preferencesManager) {
    float scale = SDL_GetWindowDisplayScale(window);
    float density = SDL_GetWindowPixelDensity(window);
    if (density <= 0.0f) density = 1.0f;
    if (scale <= 0.0f) scale = 1.0f;
    float ui_scale = scale / density;

    if (scale_with_window) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        user_ui_scale = (float)w / (float)initial_width;
        preferencesManager.SetFloat("UIScale", user_ui_scale);
    }
    ui_scale *= user_ui_scale;

    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle(); // Reset to default style to avoid compounding scales
    ImGui::StyleColorsDark();
    // Re-apply any custom theme colours on top of the freshly reset style.
    ThemeManager::GetInstance().Reapply();
    style.ScaleAllSizes(ui_scale);
    if (style.WindowBorderHoverPadding <= 0.0f) style.WindowBorderHoverPadding = 1.0f;
    ImGui::GetIO().FontGlobalScale = ui_scale * user_font_scale;
}

// Rebuild the ImGui font atlas after a theme font change.
// Must be called BEFORE ImGui_ImplSDLRenderer3_NewFrame().
// Falls back to the built-in default font if the requested file cannot be loaded.
//
// The SDL3 renderer backend advertises ImGuiBackendFlags_RendererHasTextures,
// meaning it manages the font atlas GPU texture automatically.  Calling
// io.Fonts->Build() marks the atlas as dirty; the backend will upload the new
// texture on the next frame without any explicit Create/Destroy calls needed.
void RebuildFontAtlas() {
    ImGuiIO& io = ImGui::GetIO();
    ThemeManager& theme = ThemeManager::GetInstance();

    io.Fonts->Clear();

    const std::string& fontPath = theme.GetResolvedFontPath();
    const float fontSize = theme.GetFontSize();

    bool loaded = false;
    if (!fontPath.empty()) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize);
        loaded = (f != nullptr);
        if (!loaded)
            SDL_Log("[Font] Failed to load '%s' — falling back to default.", fontPath.c_str());
    }
    if (!loaded)
        io.Fonts->AddFontDefault();

    io.Fonts->Build();
    theme.ClearPendingFontChange();
}

void DrawDeviceVisualizer(const DeviceState& dev, DeviceManager& deviceManager, PreferencesManager& preferencesManager) {
    static GamepadVisualizer gamepad_viz;
    static GenericVisualizer generic_viz;
    static SteeringWheelVisualizer wheel_viz;
    static FlightStickVisualizer flight_stick_viz;
    static GamepadHapticsVisualizer gamepad_haptics_viz;
    static SteeringWheelHapticsVisualizer wheel_haptics_viz;
    static WiimoteVisualizer wiimote_viz;

    std::string guid = DeviceManager::GetDeviceGUIDString(dev);
    bool apply_pref = !preferencesManager.IsPreferenceApplied(dev.instance_id);
    std::string preferred_viz = preferencesManager.GetVisualizerPreference(guid);

    if (apply_pref) {
        preferencesManager.MarkPreferenceApplied(dev.instance_id);
    }

    auto TabItem = [&](const char *label, DeviceVisualizer &visualizer) {
        ImGuiTabItemFlags flags = 0;
        if (apply_pref && preferred_viz == label) {
            flags |= ImGuiTabItemFlags_SetSelected;
        }

        if (ImGui::BeginTabItem(label, nullptr, flags)) {
            visualizer.Draw(dev);
            if (preferencesManager.GetVisualizerPreference(guid) != label) {
                preferencesManager.SetVisualizerPreference(guid, label);
                preferencesManager.Save();
            }
            ImGui::EndTabItem();
        }
    };

    if (dev.is_gamepad) {
        if (ImGui::BeginTabBar("DeviceMode")) {
            // TabItem("Standard Layout", gamepad_viz);
            TabItem("Raw Inputs", generic_viz);
            if (ImGui::BeginTabItem("Haptic Test")) {
                gamepad_haptics_viz.Draw(dev, deviceManager);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        if (ImGui::BeginTabBar("DeviceMode")) {
            TabItem("Raw Inputs", generic_viz);

            SDL_JoystickType type = SDL_GetJoystickType(dev.joystick);
            if (type == SDL_JOYSTICK_TYPE_WHEEL || type == SDL_JOYSTICK_TYPE_UNKNOWN) {
                // TabItem("Steering Wheel", wheel_viz);
            }
            if (type == SDL_JOYSTICK_TYPE_FLIGHT_STICK || type == SDL_JOYSTICK_TYPE_THROTTLE || type == SDL_JOYSTICK_TYPE_UNKNOWN) {
                // TabItem("Flight Stick", flight_stick_viz);
            }
            if (dev.name.find("Nintendo") != std::string::npos || dev.name.find("Wiimote") != std::string::npos) {
                TabItem("Wiimote", wiimote_viz);
            }
            if (type == SDL_JOYSTICK_TYPE_WHEEL) {
                if (ImGui::BeginTabItem("Haptic Test")) {
                    wheel_haptics_viz.Draw(dev, deviceManager);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
}

void DrawDeviceItem(const DeviceState& dev, DeviceManager& deviceManager, PreferencesManager& preferencesManager) {
    ImGui::PushID((int)dev.instance_id);
    std::string label = dev.name + " [ID: " + std::to_string(dev.instance_id) + "]" + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)");
    
    bool header_open = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    // Draw battery indicator if available
    if ((dev.battery_state != SDL_POWERSTATE_UNKNOWN || dev.battery_percent >= 0) && dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 rect_min = ImGui::GetItemRectMin();
        ImVec2 rect_max = ImGui::GetItemRectMax();
        
        float icon_h = ImGui::GetTextLineHeight();
        float icon_w = icon_h * 1.6f;
        float pad = ImGui::GetStyle().FramePadding.x;
        
        ImVec2 icon_pos = ImVec2(rect_max.x - icon_w - pad, rect_min.y + (rect_max.y - rect_min.y - icon_h) * 0.5f);
        
        ImU32 bat_col = ImGui::GetColorU32(ImGuiCol_Text);
        if (dev.battery_state == SDL_POWERSTATE_CHARGING || dev.battery_state == SDL_POWERSTATE_CHARGED) {
            bat_col = IM_COL32(50, 255, 50, 255);
        } else if (dev.battery_percent >= 0) {
            if (dev.battery_percent <= 20) bat_col = IM_COL32(255, 50, 50, 255);
            else if (dev.battery_percent <= 50) bat_col = IM_COL32(255, 200, 50, 255);
            else bat_col = IM_COL32(50, 255, 50, 255);
        }
        
        // Draw Battery Body
        float body_w = icon_w * 0.85f;
        float term_w = icon_w * 0.15f;
        float term_h = icon_h * 0.4f;
        
        draw_list->AddRect(icon_pos, icon_pos + ImVec2(body_w, icon_h), bat_col, 0.0f, 0, 2.0f);
        draw_list->AddRectFilled(icon_pos + ImVec2(body_w, (icon_h - term_h) * 0.5f), 
                                 icon_pos + ImVec2(icon_w, (icon_h + term_h) * 0.5f), bat_col);
                                 
        // Draw Level
        if (dev.battery_percent >= 0) {
            float fill_pct = dev.battery_percent / 100.0f;
            float fill_w = (body_w - 4.0f) * fill_pct;
            if (fill_w > 0) {
                draw_list->AddRectFilled(icon_pos + ImVec2(2.0f, 2.0f), 
                                         icon_pos + ImVec2(2.0f + fill_w, icon_h - 2.0f), bat_col);
            }
        }
        
        // Charging indicator
        if (dev.battery_state == SDL_POWERSTATE_CHARGING) {
            ImVec2 center = icon_pos + ImVec2(body_w * 0.5f, icon_h * 0.5f);
            draw_list->AddLine(center + ImVec2(-3, 0), center + ImVec2(3, 0), IM_COL32(255,255,255,255), 2.0f);
            draw_list->AddLine(center + ImVec2(0, -3), center + ImVec2(0, 3), IM_COL32(255,255,255,255), 2.0f);
        }
    }
    
    if (header_open) {
        ImGui::Indent();
        
        // Show detailed battery info
        if ((dev.battery_state != SDL_POWERSTATE_UNKNOWN || dev.battery_percent >= 0) && dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
            const char* state_str = "Unknown";
            switch (dev.battery_state) {
                case SDL_POWERSTATE_ON_BATTERY: state_str = "On Battery"; break;
                case SDL_POWERSTATE_NO_BATTERY: state_str = "No Battery"; break;
                case SDL_POWERSTATE_CHARGING: state_str = "Charging"; break;
                case SDL_POWERSTATE_CHARGED: state_str = "Fully Charged"; break;
                default: break;
            }
            
            ImGui::Text("Battery: %s", state_str);
            if (dev.battery_percent >= 0) {
                ImGui::SameLine();
                ImGui::Text("(%d%%)", dev.battery_percent);
                
                // Draw battery level bar
                float battery_fraction = dev.battery_percent / 100.0f;
                ImVec4 bar_color = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
                if (dev.battery_percent < 30) {
                    bar_color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                } else if (dev.battery_percent < 70) {
                    bar_color = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);
                }
                
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
                ImGui::ProgressBar(battery_fraction, ImVec2(-1, 0), "");
                ImGui::PopStyleColor();
            }
        }
        
        DrawDeviceVisualizer(dev, deviceManager, preferencesManager);
        ImGui::Unindent();
    }
    ImGui::PopID();
}

void DrawMainMenu(bool& done, bool& show_ui_settings, bool& show_protocol_editor) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Protocol...")) {
                show_protocol_editor = true;
                ProtocolEditorWindow::ShowImportDialog();
            }
            
            if (ImGui::MenuItem("Export Protocol...")) {
                show_protocol_editor = true;
                ProtocolEditorWindow::ShowExportDialog();
            }
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Exit")) {
                done = true;
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Tools")) {
            ImGui::MenuItem("Protocol Editor", NULL, &show_protocol_editor);
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Settings")) {
            ImGui::MenuItem("UI Settings", NULL, &show_ui_settings);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void DrawSettingsWindow(bool& show_ui_settings, float& user_ui_scale, float& user_font_scale, bool& scale_with_window, SDL_Window* window, int initial_width, int initial_height, PreferencesManager& preferencesManager) {
    if (!show_ui_settings) return;

    ImGui::Begin("UI Settings", &show_ui_settings, ImGuiWindowFlags_AlwaysAutoResize);

    // ------------------------------------------------------------------
    // UI Scale controls
    // ------------------------------------------------------------------
    bool changed = false;
    bool scale_changed = false;
    if (ImGui::Button("-##UI")) {
        user_ui_scale -= 0.05f;
        if (user_ui_scale < 0.5f) user_ui_scale = 0.5f;
        scale_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+##UI")) {
        user_ui_scale += 0.05f;
        if (user_ui_scale > 3.0f) user_ui_scale = 3.0f;
        scale_changed = true;
    }
    ImGui::SameLine();
    ImGui::Text("UI Scale: %.2f", user_ui_scale);

    // ------------------------------------------------------------------
    // Font Scale controls
    // ------------------------------------------------------------------
    bool font_scale_changed = false;
    if (ImGui::Button("-##Font")) {
        user_font_scale -= 0.05f;
        if (user_font_scale < 0.5f) user_font_scale = 0.5f;
        font_scale_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+##Font")) {
        user_font_scale += 0.05f;
        if (user_font_scale > 3.0f) user_font_scale = 3.0f;
        font_scale_changed = true;
    }
    ImGui::SameLine();
    ImGui::Text("Font Scale: %.2f", user_font_scale);

    if (scale_changed) {
        scale_with_window = false;
        changed = true;
    }
    if (ImGui::Checkbox("Scale with Window", &scale_with_window)) changed = true;

    if (ImGui::Button("Reset UI")) {
        user_ui_scale = 1.0f;
        user_font_scale = 1.0f;
        scale_with_window = false;
        SDL_SetWindowSize(window, initial_width, initial_height);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        changed = true;
    }

    if (changed || font_scale_changed) {
        preferencesManager.SetFloat("UIScale", user_ui_scale);
        preferencesManager.SetFloat("FontScale", user_font_scale);
        preferencesManager.SetBool("ScaleWithWindow", scale_with_window);
        preferencesManager.Save();
        UpdateUIScale(window, user_ui_scale, user_font_scale, scale_with_window, initial_width, preferencesManager);
    }

    // ------------------------------------------------------------------
    // Colour Theme dropdown
    // ------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Colour Theme");

    ThemeManager& theme = ThemeManager::GetInstance();
    const auto& entries = theme.GetAvailableThemes();

    // combo index: 0 = Default (Dark), 1..N = entries[0..N-1]
    int comboIndex = theme.HasCustomTheme() ? theme.GetCurrentEntryIndex() + 1 : 0;

    // Build a flat list of C-strings for ImGui::Combo.
    // We do this inline rather than caching so a Refresh is instantly reflected.
    std::vector<const char*> comboItems;
    comboItems.reserve(entries.size() + 1);
    comboItems.push_back("Default (Dark)");
    for (const auto& e : entries)
        comboItems.push_back(e.displayName.c_str());

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("##ThemeCombo", &comboIndex,
                     comboItems.data(), (int)comboItems.size())) {
        if (comboIndex == 0) {
            // Revert to built-in style
            theme.ApplyDefault();
            theme.SaveToPreferences(preferencesManager);
            preferencesManager.Save();
            UpdateUIScale(window, user_ui_scale, user_font_scale, scale_with_window, initial_width, preferencesManager);
        } else {
            int entryIdx = comboIndex - 1;
            if (entryIdx >= 0 && entryIdx < (int)entries.size()) {
                auto result = theme.LoadFromFile(entries[entryIdx].path);
                if (result.IsOk()) {
                    theme.SaveToPreferences(preferencesManager);
                    preferencesManager.Save();
                    UpdateUIScale(window, user_ui_scale, user_font_scale, scale_with_window, initial_width, preferencesManager);
                }
                // On failure the error is shown in the banner below and the
                // combo cursor stays on the previously valid selection.
                comboIndex = theme.HasCustomTheme() ? theme.GetCurrentEntryIndex() + 1 : 0;
            }
        }
    }

    // Refresh button – re-scans the themes/ folder
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        theme.Refresh();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rescan the themes/ folder for new .json files");

    // Hint showing where to place theme files
    if (entries.empty()) {
        ImGui::TextDisabled("No themes found — place .json files in the themes/ folder");
    }

    // Error banner
    if (!theme.GetLastError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("Error: %s", theme.GetLastError().c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void SetupDockSpace() {
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    static bool first_time = true;
    if (first_time) {
        first_time = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);
        ImGuiID dock_id_left, dock_id_right;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.5f, &dock_id_left, &dock_id_right);
        ImGui::DockBuilderDockWindow("Devices", dock_id_left);
        ImGui::DockBuilderDockWindow("Network Server", dock_id_right);
        ImGui::DockBuilderDockWindow("Output Mapper", dock_id_right);
        ImGui::DockBuilderDockWindow("Input Mapper", dock_id_right);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
}

void DrawDevicesWindow(DeviceManager& deviceManager, PreferencesManager& preferencesManager, bool& vsync, int& framerate_limit, SDL_Renderer* renderer, const ImGuiIO& io) {
    ImGui::Begin("Devices");
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

    if (ImGui::Checkbox("VSync", &vsync)) {
        SDL_SetRenderVSync(renderer, vsync ? 1 : 0);
    }
    ImGui::SameLine();
    const char* label = "Framerate Limit";
    float width = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemInnerSpacing.x;
    if (width < 10.0f) width = 10.0f;
    ImGui::SetNextItemWidth(width);
    ImGui::InputInt(label, &framerate_limit);
    if (framerate_limit < 0) {
        framerate_limit = 0;
    }

    static bool first_run = true;
    static bool enable_battery_led = true;
    if (first_run) {
        enable_battery_led = preferencesManager.GetBool("EnableBatteryLED", true);
        first_run = false;
    }
    if (ImGui::Checkbox("Battery LED Indicator", &enable_battery_led)) {
        preferencesManager.SetBool("EnableBatteryLED", enable_battery_led);
        preferencesManager.Save();
    }

    ImGui::Separator();
    auto &devices = deviceManager.GetDevices();
    ImGui::Text("Connected Devices: %d", (int)devices.size());

    // Update battery info periodically (every 60 frames / ~1 second at 60fps)
    static int frame_counter = 0;
    if (frame_counter++ >= 60) {
        frame_counter = 0;
        for (auto &dev : const_cast<std::vector<DeviceState>&>(devices)) {
            deviceManager.UpdateBatteryInfo(dev);

            // Update LED based on battery level
            if (enable_battery_led && dev.gamepad) {
                Uint8 r = 0, g = 0, b = 0;
                bool update_led = false;

                if (dev.battery_state == SDL_POWERSTATE_CHARGING) {
                    // Blue for charging
                    r = 0; g = 0; b = 255;
                    update_led = true;
                } else if (dev.battery_state == SDL_POWERSTATE_CHARGED) {
                    // Green for fully charged
                    r = 0; g = 255; b = 0;
                    update_led = true;
                } else if (dev.battery_state != SDL_POWERSTATE_UNKNOWN && dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
                    if (dev.battery_percent >= 70) {
                        r = 0; g = 255; b = 0; // Green
                    } else if (dev.battery_percent >= 30) {
                        r = 255; g = 165; b = 0; // Yellow/Orange
                    } else {
                        r = 255; g = 0; b = 0; // Red
                    }
                    update_led = true;
                }

                if (update_led) {
                    SDL_SetGamepadLED(dev.gamepad, r, g, b);
                }
            }
        }
    }

    for (const auto &dev : devices) {
        DrawDeviceItem(dev, deviceManager, preferencesManager);
    }
    ImGui::End();
}

void ProcessEvents(bool& done, SDL_Window* window, DeviceManager& deviceManager, PreferencesManager& preferencesManager, float& user_ui_scale, float& user_font_scale, bool scale_with_window, int initial_width) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT)
            done = true;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(window))
            done = true;

        if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED &&
            event.window.windowID == SDL_GetWindowID(window)) {
            UpdateUIScale(window, user_ui_scale, user_font_scale, scale_with_window, initial_width, preferencesManager);
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED && scale_with_window &&
            event.window.windowID == SDL_GetWindowID(window)) {
            UpdateUIScale(window, user_ui_scale, user_font_scale, scale_with_window, initial_width, preferencesManager);
        }

        // Handle hot-plugging
        if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
            bool is_connected = false;
            for (const auto &dev : deviceManager.GetDevices()) {
                if (dev.instance_id == event.jdevice.which) {
                    is_connected = true;
                    break;
                }
            }
            if (!is_connected) {
                deviceManager.HandleDeviceAdded(event.jdevice.which);
            }
        }
        if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
            deviceManager.HandleDeviceRemoved(event.jdevice.which);
            preferencesManager.ClearAppliedPreference(event.jdevice.which);
        }
    }
}

void RenderFrame(SDL_Renderer* renderer, SDL_Window* window, bool vsync, int framerate_limit, Uint64 frame_start_time) {
    ImGui::Render();

    int w, h, bbw, bbh;
    SDL_GetWindowSize(window, &w, &h);
    SDL_GetWindowSizeInPixels(window, &bbw, &bbh);
    float scale_x = (w > 0) ? ((float)bbw / w) : 1.0f;
    float scale_y = (h > 0) ? ((float)bbh / h) : 1.0f;
    SDL_SetRenderScale(renderer, scale_x, scale_y);

    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    if (!vsync && framerate_limit > 0) {
        Uint64 frame_end_time = SDL_GetTicks();
        Uint64 frame_duration = frame_end_time - frame_start_time;
        Uint64 target_duration = 1000 / framerate_limit;
        if (frame_duration < target_duration) {
            SDL_Delay((Uint32)(target_duration - frame_duration));
        }
    }
}

int main(int argc, char *argv[]) {
    // Register protocols
    ProtocolManager::GetInstance().RegisterProtocol(
        std::make_shared<OSCProtocol>());
#if ENABLE_WEBSOCKETS
    ProtocolManager::GetInstance().RegisterProtocol(
        std::make_shared<WebSocketProtocol>());
#endif

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // This tells SDL to use its own built-in driver for Steam Controllers
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");
    // This ensures SDL handles the rumble translation itself
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM_HOME_LED, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");

    // Setup SDL3 with Joystick and Gamepad support
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

    const int initial_width = 1280;
    const int initial_height = 720;
    // Create window with SDL3 flags
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window = SDL_CreateWindow("InputBridge", initial_width, initial_height, window_flags);
    if (!window) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }

    // Create SDL_Renderer3 (The SDL3 version of the renderer)
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        printf("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        return -1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad
    // Controls

    static std::string ini_filename;
    const char *base_path = SDL_GetBasePath();
    if (base_path) {
        ini_filename = std::string(base_path) + "imgui.ini";
    } else {
        ini_filename = "imgui.ini";
    }
    io.IniFilename = ini_filename.c_str();

    ImGui::StyleColorsDark();

    // Setup Backends for SDL3 + SDL_Renderer3
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    DeviceManager& deviceManager = DeviceManager::GetInstance();
    PreferencesManager preferencesManager;

    // Initial device scan
    int count = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&count);
    if (joysticks) {
        for (int i = 0; i < count; i++) {
            deviceManager.HandleDeviceAdded(joysticks[i]);
        }
        SDL_free(joysticks);
    }

    preferencesManager.Load();

    float user_ui_scale = preferencesManager.GetFloat("UIScale", 1.0f);
    float user_font_scale = preferencesManager.GetFloat("FontScale", 1.0f);
    bool scale_with_window = preferencesManager.GetBool("ScaleWithWindow", false);

    // Scan and restore the saved theme BEFORE UpdateUIScale.
    // UpdateUIScale resets ImGuiStyle and then calls ThemeManager::Reapply()
    // before running ScaleAllSizes(ui_scale).  If the theme is loaded here,
    // Reapply() correctly overlays the theme's style values (e.g. rounding)
    // onto the fresh default style, and ScaleAllSizes then scales them properly.
    // Loading the theme AFTER UpdateUIScale causes ApplyData to write absolute
    // values directly onto the already-DPI-scaled style — so rounding at startup
    // would differ from rounding after switching themes at runtime.
    {
        std::string scanBase = base_path ? std::string(base_path) : ".";
        ThemeManager::GetInstance().ScanThemesDirectory(scanBase);
    }
    ThemeManager::GetInstance().LoadFromPreferences(preferencesManager);

    UpdateUIScale(window, user_ui_scale, user_font_scale, scale_with_window, initial_width, preferencesManager);

    // Rebuild the font atlas if the restored theme specifies a custom font.
    // Must happen after UpdateUIScale (which sets FontGlobalScale) but before
    // the first frame.
    if (ThemeManager::GetInstance().HasPendingFontChange())
        RebuildFontAtlas();

    OutputMapper::Init(deviceManager);
    OutputMapper& outputMapper = OutputMapper::GetInstance();

    InputMapper::Init(deviceManager);
    InputMapper& inputMapper = InputMapper::GetInstance();
    inputMapper.LoadConfig(preferencesManager);

    // Load protocol definitions FIRST so servers can restore their saved definition selection
    ProtocolRegistry::GetInstance().LoadAll();

    // Load Network Server Configs
    OSCServer::GetInstance().LoadConfig(preferencesManager);
    WebSocketServer::GetInstance().LoadConfig(preferencesManager);

    bool done = false;
    bool vsync = true;
    int framerate_limit = 60;
    static bool show_ui_settings = false;
    static bool show_protocol_editor = false;
    SDL_SetRenderVSync(renderer, 1);

    WebSocketServer::GetInstance().SetOutputMapper(&outputMapper);
    OSCServer::GetInstance().SetOutputMapper(&outputMapper);

    static int server_update_rate = preferencesManager.GetInt("Network", "UpdateRate", 60);
    static bool server_dynamic_rate = preferencesManager.GetBool("Network", "DynamicRate", false);
    static Uint64 last_server_update_time = 0;
    static int messages_sent_counter = 0;
    static float current_messages_per_second = 0.0f;
    static Uint64 last_mps_update_time = SDL_GetTicks();

    while (!done) {
        Uint64 frame_start_time = SDL_GetTicks();
        ProcessEvents(done, window, deviceManager, preferencesManager, user_ui_scale, user_font_scale, scale_with_window, initial_width);

        // Always update haptics to ensure low latency
        outputMapper.Update();

        bool check_input_update = false;
        if (server_dynamic_rate) {
            check_input_update = true;
        } else {
            if (server_update_rate > 0) {
                Uint64 interval = 1000 / server_update_rate;
                if (frame_start_time - last_server_update_time >= interval) {
                    check_input_update = true;
                }
            }
        }

        if (check_input_update && inputMapper.Update(server_dynamic_rate)) {
            last_server_update_time = frame_start_time;
            messages_sent_counter++;
        }

        if (frame_start_time - last_mps_update_time >= 1000) {
            current_messages_per_second = (float)messages_sent_counter / ((frame_start_time - last_mps_update_time) / 1000.0f);
            messages_sent_counter = 0;
            last_mps_update_time = frame_start_time;
        }

        // Rebuild font atlas if a theme change requested a different font.
        // Must happen before ImGui_ImplSDLRenderer3_NewFrame().
        if (ThemeManager::GetInstance().HasPendingFontChange())
            RebuildFontAtlas();

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Menu Bar
        DrawMainMenu(done, show_ui_settings, show_protocol_editor);

        DrawSettingsWindow(show_ui_settings, user_ui_scale, user_font_scale, scale_with_window, window, initial_width, initial_height, preferencesManager);
        
        // Protocol Editor
        ProtocolEditorWindow::Draw(show_protocol_editor);

        // DockSpace
        SetupDockSpace();

        DrawDevicesWindow(deviceManager, preferencesManager, vsync, framerate_limit, renderer, io);

        inputMapper.DrawContent();
        outputMapper.DrawContent();

        NetworkStatusWindow::Draw(server_update_rate, server_dynamic_rate, current_messages_per_second);

        // Rendering
        RenderFrame(renderer, window, vsync, framerate_limit, frame_start_time);
    }

    // Cleanup
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    inputMapper.SaveConfig(preferencesManager);

    InputMapper::Shutdown();
    OutputMapper::Shutdown();

    // Save Network Server Configs
    OSCServer::GetInstance().SaveConfig(preferencesManager);
    WebSocketServer::GetInstance().SaveConfig(preferencesManager);

    // Persist any unsaved protocol definitions
    ProtocolRegistry::GetInstance().SaveAll();

    preferencesManager.SetInt("Network", "UpdateRate", server_update_rate);
    preferencesManager.SetBool("Network", "DynamicRate", server_dynamic_rate);

    preferencesManager.Save();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}