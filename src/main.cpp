#include <SDL3/SDL.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <stdio.h>
#include <vector>
#include <string>

#include "DeviceState.h"
#include "GamepadVisualizer.h"
#include "GenericVisualizer.h"
#include "SteeringWheelVisualizer.h"
#include "FlightStickVisualizer.h"

// Note: For SDL3, we may need to link against SDL3_net if we want use it.
// #include <SDL3_net/SDL_net.h>

std::vector<DeviceState> g_Devices;

void HandleDeviceAdded(SDL_JoystickID instance_id) {
    if (SDL_IsGamepad(instance_id)) {
        SDL_Gamepad* gamepad = SDL_OpenGamepad(instance_id);
        if (gamepad) {
            DeviceState dev;
            dev.instance_id = instance_id;
            dev.name = SDL_GetGamepadName(gamepad);
            dev.is_gamepad = true;
            dev.gamepad = gamepad;
            dev.joystick = SDL_GetGamepadJoystick(gamepad);
            dev.num_axes = SDL_GetNumJoystickAxes(dev.joystick);
            dev.num_buttons = SDL_GetNumJoystickButtons(dev.joystick);
            dev.num_hats = SDL_GetNumJoystickHats(dev.joystick);
            g_Devices.push_back(dev);
        }
    } else {
        SDL_Joystick* joystick = SDL_OpenJoystick(instance_id);
        if (joystick) {
            DeviceState dev;
            dev.instance_id = instance_id;
            dev.name = SDL_GetJoystickName(joystick);
            dev.is_gamepad = false;
            dev.gamepad = nullptr;
            dev.joystick = joystick;
            dev.num_axes = SDL_GetNumJoystickAxes(joystick);
            dev.num_buttons = SDL_GetNumJoystickButtons(joystick);
            dev.num_hats = SDL_GetNumJoystickHats(joystick);
            g_Devices.push_back(dev);
        }
    }
}

void HandleDeviceRemoved(SDL_JoystickID instance_id) {
    for (auto it = g_Devices.begin(); it != g_Devices.end(); ++it) {
        if (it->instance_id == instance_id) {
            if (it->gamepad) SDL_CloseGamepad(it->gamepad);
            else if (it->joystick) SDL_CloseJoystick(it->joystick);
            g_Devices.erase(it);
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    // Setup SDL3 with Joystick and Gamepad support
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

    // Create window with SDL3 flags
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("InputBridge Debugger (SDL3)", 1280, 720, window_flags);
    if (!window) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }

    // Create SDL_Renderer3 (The SDL3 version of the renderer)
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        printf("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        return -1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    ImGui::StyleColorsDark();

    // Setup Backends for SDL3 + SDL_Renderer3
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Initial device scan
    int count = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&count);
    if (joysticks) {
        for (int i = 0; i < count; i++) {
            HandleDeviceAdded(joysticks[i]);
        }
        SDL_free(joysticks);
    }

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
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
            
            // Handle hot-plugging
            if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
                HandleDeviceAdded(event.jdevice.which);
            }
            if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
                HandleDeviceRemoved(event.jdevice.which);
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
        if (framerate_limit < 0) framerate_limit = 0;
        
        ImGui::Separator();
        ImGui::Text("Connected Devices: %d", (int)g_Devices.size());
        
        for (const auto& dev : g_Devices) {
            ImGui::PushID((int)dev.instance_id);
            if (ImGui::CollapsingHeader((dev.name + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)")).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                
                static GamepadVisualizer gamepad_viz;
                static GenericVisualizer generic_viz;
                static SteeringWheelVisualizer wheel_viz;
                static FlightStickVisualizer flight_stick_viz;

                if (dev.is_gamepad) {
                    if (ImGui::BeginTabBar("DeviceMode")) {
                        if (ImGui::BeginTabItem("Standard Layout")) {
                            gamepad_viz.Draw(dev);
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Raw Inputs")) {
                            generic_viz.Draw(dev);
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                } else {
                    if (ImGui::BeginTabBar("DeviceMode")) {
                        if (ImGui::BeginTabItem("Raw Inputs")) {
                            generic_viz.Draw(dev);
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Steering Wheel")) {
                            wheel_viz.Draw(dev);
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Flight Stick")) {
                            flight_stick_viz.Draw(dev);
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }
                
                // TODO: Serialize dev.axes and dev.buttons here and send via OSC/Websocket

                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        if (ImGui::Button("Exit")) done = true;
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
    
    // Close all devices
    for (auto& dev : g_Devices) {
        if (dev.gamepad) SDL_CloseGamepad(dev.gamepad);
        else if (dev.joystick) SDL_CloseJoystick(dev.joystick);
    }
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}