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
#include "Protocols/OSCProtocol.h"
#include "Network/OSCServer.h"
#include "Network/WebSocketServer.h"
#include "Protocols/ProtocolManager.h"
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

void UpdateUIScale(SDL_Window *window, float& user_ui_scale, bool scale_with_window, int initial_width, PreferencesManager& preferencesManager) {
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
    style.ScaleAllSizes(ui_scale);
    if (style.WindowBorderHoverPadding <= 0.0f) style.WindowBorderHoverPadding = 1.0f;
    ImGui::GetIO().FontGlobalScale = ui_scale;
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
    
    // Add battery indicator if available
    if (dev.battery_state != SDL_POWERSTATE_UNKNOWN && dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
        ImVec4 battery_color;
        const char* battery_icon;
        
        if (dev.battery_state == SDL_POWERSTATE_CHARGING) {
            battery_color = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // Green for charging
            battery_icon = " 🔌";
        } else if (dev.battery_state == SDL_POWERSTATE_CHARGED) {
            battery_color = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // Green for fully charged
            battery_icon = " ⚡";
        } else { // ON_BATTERY
            if (dev.battery_percent >= 70) {
                battery_color = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // Green
                battery_icon = " 🔋";
            } else if (dev.battery_percent >= 30) {
                battery_color = ImVec4(1.0f, 1.0f, 0.2f, 1.0f); // Yellow
                battery_icon = " 🔋";
            } else {
                battery_color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Red
                battery_icon = " 🪫";
            }
        }
        
        label += battery_icon;
        if (dev.battery_percent >= 0) {
            label += " " + std::to_string(dev.battery_percent) + "%";
        }
    }
    
    if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        
        // Show detailed battery info
        if (dev.battery_state != SDL_POWERSTATE_UNKNOWN && dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
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

void DrawMainMenu(bool& done, bool& show_ui_settings) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                done = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Settings")) {
            ImGui::MenuItem("UI Settings", NULL, &show_ui_settings);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void DrawSettingsWindow(bool& show_ui_settings, float& user_ui_scale, bool& scale_with_window, SDL_Window* window, int initial_width, int initial_height, PreferencesManager& preferencesManager) {
    if (!show_ui_settings) return;

    ImGui::Begin("UI Settings", &show_ui_settings, ImGuiWindowFlags_AlwaysAutoResize);
    bool changed = false;
    bool scale_changed = false;
    if (ImGui::Button("-")) {
        user_ui_scale -= 0.05f;
        if (user_ui_scale < 0.5f) user_ui_scale = 0.5f;
        scale_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        user_ui_scale += 0.05f;
        if (user_ui_scale > 3.0f) user_ui_scale = 3.0f;
        scale_changed = true;
    }
    ImGui::SameLine();
    ImGui::Text("UI Scale: %.2f", user_ui_scale);

    if (scale_changed) {
        scale_with_window = false;
        changed = true;
    }
    if (ImGui::Checkbox("Scale with Window", &scale_with_window)) changed = true;

    if (ImGui::Button("Reset UI")) {
        user_ui_scale = 1.0f;
        scale_with_window = false;
        SDL_SetWindowSize(window, initial_width, initial_height);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        changed = true;
    }

    if (changed) {
        preferencesManager.SetFloat("UIScale", user_ui_scale);
        preferencesManager.SetBool("ScaleWithWindow", scale_with_window);
        preferencesManager.Save();
        UpdateUIScale(window, user_ui_scale, scale_with_window, initial_width, preferencesManager);
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
        ImGuiID dock_id_right_top, dock_id_right_bottom;
        ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Up, 0.5f, &dock_id_right_top, &dock_id_right_bottom);
        ImGui::DockBuilderDockWindow("Devices", dock_id_left);
        ImGui::DockBuilderDockWindow("Network Server", dock_id_right_top);
        ImGui::DockBuilderDockWindow("Output Mapper", dock_id_right_bottom);
        ImGui::DockBuilderDockWindow("Input Mapper", dock_id_right_bottom);
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

void ProcessEvents(bool& done, SDL_Window* window, DeviceManager& deviceManager, PreferencesManager& preferencesManager, float& user_ui_scale, bool scale_with_window, int initial_width) {
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
            UpdateUIScale(window, user_ui_scale, scale_with_window, initial_width, preferencesManager);
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED && scale_with_window &&
            event.window.windowID == SDL_GetWindowID(window)) {
            UpdateUIScale(window, user_ui_scale, scale_with_window, initial_width, preferencesManager);
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
    bool scale_with_window = preferencesManager.GetBool("ScaleWithWindow", false);

    UpdateUIScale(window, user_ui_scale, scale_with_window, initial_width, preferencesManager);

    OutputMapper::Init(deviceManager);
    OutputMapper& outputMapper = OutputMapper::GetInstance();

    InputMapper::Init(deviceManager);
    InputMapper& inputMapper = InputMapper::GetInstance();
    inputMapper.LoadConfig(preferencesManager);

    // Load Network Server Configs
    OSCServer::GetInstance().LoadConfig(preferencesManager);
    WebSocketServer::GetInstance().LoadConfig(preferencesManager);

    bool done = false;
    bool vsync = true;
    int framerate_limit = 60;
    static bool show_ui_settings = false;
    SDL_SetRenderVSync(renderer, 1);

    WebSocketServer::GetInstance().SetOutputMapper(&outputMapper);
    OSCServer::GetInstance().SetOutputMapper(&outputMapper);

    static int server_update_rate = 60;
    static bool server_dynamic_rate = false;
    static Uint64 last_server_update_time = 0;
    static int messages_sent_counter = 0;
    static float current_messages_per_second = 0.0f;
    static Uint64 last_mps_update_time = SDL_GetTicks();

    while (!done) {
        Uint64 frame_start_time = SDL_GetTicks();
        ProcessEvents(done, window, deviceManager, preferencesManager, user_ui_scale, scale_with_window, initial_width);

        bool should_update_server = false;
        if (server_dynamic_rate) {
            should_update_server = true;
        } else {
            if (server_update_rate > 0) {
                Uint64 interval = 1000 / server_update_rate;
                if (frame_start_time - last_server_update_time >= interval) {
                    should_update_server = true;
                }
            }
        }

        if (should_update_server) {
            outputMapper.Update();
            last_server_update_time = frame_start_time;
            messages_sent_counter++;
        }

        if (frame_start_time - last_mps_update_time >= 1000) {
            current_messages_per_second = (float)messages_sent_counter / ((frame_start_time - last_mps_update_time) / 1000.0f);
            messages_sent_counter = 0;
            last_mps_update_time = frame_start_time;
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Menu Bar
        DrawMainMenu(done, show_ui_settings);

        DrawSettingsWindow(show_ui_settings, user_ui_scale, scale_with_window, window, initial_width, initial_height, preferencesManager);

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
    preferencesManager.Save();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
