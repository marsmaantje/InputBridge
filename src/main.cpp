#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <stdio.h>
#include <vector>
#include <string>

// Note: For SDL3, ensure you link against SDL3_net if you use it.
// #include <SDL3_net/SDL_net.h>

struct DeviceState {
    SDL_JoystickID instance_id;
    std::string name;
    bool is_gamepad;
    SDL_Joystick* joystick;
    SDL_Gamepad* gamepad;
    int num_axes;
    int num_buttons;
};

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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

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
    while (!done) {
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
        
        ImGui::Separator();
        ImGui::Text("Connected Devices: %d", (int)g_Devices.size());
        
        for (const auto& dev : g_Devices) {
            if (ImGui::CollapsingHeader((dev.name + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)") + "##" + std::to_string(dev.instance_id)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                
                // Display Axes
                ImGui::Text("Axes (%d):", dev.num_axes);
                for (int i = 0; i < dev.num_axes; i++) {
                    Sint16 axis_val = SDL_GetJoystickAxis(dev.joystick, i);
                    float norm_val = (float)axis_val / 32767.0f;
                    ImGui::Text("Axis %d:", i); ImGui::SameLine();
                    ImGui::ProgressBar((norm_val + 1.0f) * 0.5f, ImVec2(-1, 0), std::to_string(axis_val).c_str());
                }

                // Display Buttons
                ImGui::Text("Buttons (%d):", dev.num_buttons);
                for (int i = 0; i < dev.num_buttons; i++) {
                    if (i > 0 && i % 8 != 0) ImGui::SameLine();
                    bool pressed = SDL_GetJoystickButton(dev.joystick, i);
                    ImGui::Button((std::to_string(i) + (pressed ? "*" : "")).c_str(), ImVec2(30, 0));
                    if (pressed) {
                        // Visual feedback
                        ImDrawList* draw_list = ImGui::GetWindowDrawList();
                        ImVec2 p_min = ImGui::GetItemRectMin();
                        ImVec2 p_max = ImGui::GetItemRectMax();
                        draw_list->AddRect(p_min, p_max, IM_COL32(255, 255, 0, 255));
                    }
                }
                
                // TODO: Serialize dev.axes and dev.buttons here and send via OSC/Websocket

                ImGui::Unindent();
            }
        }

        if (ImGui::Button("Exit")) done = true;
        ImGui::End();

        // Rendering
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
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