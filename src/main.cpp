#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <stdio.h>
#include <string>
#include <vector>

#include "Devices/DeviceManager.h"
#include "Devices/DeviceState.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/SteeringWheelHaptics.h"
#include "Mappers/InputMapper.h"
#include "Preferences/Preferences.h"
#include "Protocols/OSCProtocol.h"
#include "Protocols/ProtocolManager.h"
#if ENABLE_WEBSOCKETS
#include "Protocols/WebSocketProtocol.h"
#endif
#include "Visualizers/FlightStickVisualizer.h"
#include "Visualizers/GamepadVisualizer.h"
#include "Visualizers/GenericVisualizer.h"
#include "Visualizers/SteeringWheelVisualizer.h"

// Note: For SDL3, we may need to link against SDL3_net if we want use it.
// #include <SDL3_net/SDL_net.h>

int main(int argc, char *argv[]) {
    // Register protocols
    ProtocolManager::GetInstance().RegisterProtocol(
        std::make_shared<OSCProtocol>());
#if ENABLE_WEBSOCKETS
    ProtocolManager::GetInstance().RegisterProtocol(
        std::make_shared<WebSocketProtocol>());
#endif

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");

    // Setup SDL3 with Joystick and Gamepad support
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

    // Create window with SDL3 flags
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window = SDL_CreateWindow("InputBridge Debugger (SDL3)", 1280,
                                          720, window_flags);
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
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad
    // Controls

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

    InputMapper inputMapper(deviceManager);
    inputMapper.LoadConfig(preferencesManager);

    bool done = false;
    bool vsync = true;
    int framerate_limit = 60;
    SDL_SetRenderVSync(renderer, 1);

    while (!done) {
        Uint64 frame_start_time = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;

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

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("InputBridge Status");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

        if (ImGui::Checkbox("VSync", &vsync)) {
            SDL_SetRenderVSync(renderer, vsync ? 1 : 0);
        }
        ImGui::InputInt("Framerate Limit", &framerate_limit);
        if (framerate_limit < 0) {
            framerate_limit = 0;
        }

        ImGui::Separator();
        const auto &devices = deviceManager.GetDevices();
        ImGui::Text("Connected Devices: %d", (int)devices.size());

        for (const auto &dev : devices) {
            ImGui::PushID((int)dev.instance_id);
            std::string label = dev.name + " [ID: " + std::to_string(dev.instance_id) + "]" + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)");
            if (ImGui::CollapsingHeader(
                    label.c_str(),
                    ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                HapticDevice* haptic = deviceManager.GetHapticDevice(dev.instance_id);
                if (haptic) {
                    if (haptic->IsReady()) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Haptics: Ready");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Haptics: Not Available");
                    }
                } else {
                    ImGui::TextDisabled("Haptics: Not Supported");
                }

                static GamepadVisualizer gamepad_viz;
                static GenericVisualizer generic_viz;
                static SteeringWheelVisualizer wheel_viz;
                static FlightStickVisualizer flight_stick_viz;

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
                        TabItem("Standard Layout", gamepad_viz);
                        TabItem("Raw Inputs", generic_viz);
                        ImGui::EndTabBar();
                    }

                    ImGui::Separator();
                    ImGui::Text("Haptics Test");
                    static float low_freq = 0.5f;
                    static float high_freq = 0.5f;
                    static int duration = 1000;
                    ImGui::SliderFloat("Low Freq", &low_freq, 0.0f, 1.0f);
                    ImGui::SliderFloat("High Freq", &high_freq, 0.0f, 1.0f);
                    ImGui::SliderInt("Duration (ms)", &duration, 0, 5000);

                    if (ImGui::Button("Play Rumble")) {
                        HapticDevice *haptic = deviceManager.GetHapticDevice(dev.instance_id);
                        if (haptic) {
                            if (auto *gamepadHaptics = dynamic_cast<GamepadHaptics *>(haptic)) {
                                gamepadHaptics->Rumble(low_freq, high_freq, (uint32_t)duration);
                            }
                        }
                    }
                } else {
                    if (ImGui::BeginTabBar("DeviceMode")) {
                        TabItem("Raw Inputs", generic_viz);

                        SDL_JoystickType type =
                            SDL_GetJoystickType(dev.joystick);
                        if (type == SDL_JOYSTICK_TYPE_WHEEL ||
                            type == SDL_JOYSTICK_TYPE_UNKNOWN) {
                            TabItem("Steering Wheel", wheel_viz);
                        }
                        if (type == SDL_JOYSTICK_TYPE_FLIGHT_STICK ||
                            type == SDL_JOYSTICK_TYPE_THROTTLE ||
                            type == SDL_JOYSTICK_TYPE_UNKNOWN) {
                            TabItem("Flight Stick", flight_stick_viz);
                        }
                        ImGui::EndTabBar();
                    }

                    if (SDL_GetJoystickType(dev.joystick) == SDL_JOYSTICK_TYPE_WHEEL) {
                        ImGui::Separator();
                        ImGui::Text("Haptics Test");

                        HapticDevice *haptic = deviceManager.GetHapticDevice(dev.instance_id);
                        if (auto *wheelHaptics = dynamic_cast<SteeringWheelHaptics *>(haptic)) {
                            if (ImGui::TreeNode("Constant Force")) {
                                static float strength = 0.5f;
                                static int duration = 1000;
                                ImGui::SliderFloat("Strength", &strength, -1.0f, 1.0f);
                                ImGui::SliderInt("Duration (ms)", &duration, 0, 5000);
                                if (ImGui::Button("Play Constant")) {
                                    wheelHaptics->PlayConstant(strength, (uint32_t)duration);
                                }
                                ImGui::TreePop();
                            }

                            if (ImGui::TreeNode("Periodic (Sine)")) {
                                static float strength = 1.0f;
                                static int period = 1000;
                                static float magnitude = 0.5f;
                                static float offset = 0.0f;
                                static int phase = 0;
                                static int duration = 1000;

                                ImGui::SliderFloat("Strength", &strength, 0.0f, 1.0f);
                                ImGui::SliderInt("Period (ms)", &period, 1, 5000);
                                ImGui::SliderFloat("Magnitude", &magnitude, 0.0f, 1.0f);
                                ImGui::SliderFloat("Offset", &offset, -1.0f, 1.0f);
                                ImGui::SliderInt("Phase", &phase, 0, 36000);
                                ImGui::SliderInt("Duration (ms)", &duration, 0, 5000);

                                if (ImGui::Button("Play Periodic")) {
                                    wheelHaptics->PlayPeriodic(strength, (uint32_t)period, magnitude, offset, (uint32_t)phase, (uint32_t)duration);
                                }
                                ImGui::TreePop();
                            }

                            if (ImGui::TreeNode("Condition (Spring)")) {
                                static float right_sat = 1.0f;
                                static float left_sat = 1.0f;
                                static float right_coeff = 0.5f;
                                static float left_coeff = 0.5f;
                                static float deadband = 0.1f;
                                static float center = 0.0f;
                                static int duration = 5000;

                                ImGui::SliderFloat("Right Sat", &right_sat, 0.0f, 1.0f);
                                ImGui::SliderFloat("Left Sat", &left_sat, 0.0f, 1.0f);
                                ImGui::SliderFloat("Right Coeff", &right_coeff, -1.0f, 1.0f);
                                ImGui::SliderFloat("Left Coeff", &left_coeff, -1.0f, 1.0f);
                                ImGui::SliderFloat("Deadband", &deadband, 0.0f, 1.0f);
                                ImGui::SliderFloat("Center", &center, -1.0f, 1.0f);
                                ImGui::SliderInt("Duration (ms)", &duration, 0, 10000);

                                if (ImGui::Button("Play Spring")) {
                                    wheelHaptics->PlayCondition(right_sat, left_sat, right_coeff, left_coeff, deadband, center, (uint32_t)duration);
                                }
                                ImGui::TreePop();
                            }
                        } else {
                            ImGui::TextDisabled("Haptics not available");
                        }
                    }
                }

                // TODO: Serialize dev.axes and dev.buttons here and send via
                // OSC/Websocket

                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        inputMapper.DrawUI();

        if (ImGui::Button("Exit"))
            done = true;
        ImGui::End();

        // Rendering
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

    // Cleanup
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    inputMapper.SaveConfig(preferencesManager);
    preferencesManager.Save();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}