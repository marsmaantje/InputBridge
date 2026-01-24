#include <SDL3/SDL.h>
#define IMGUI_DEFINE_MATH_OPERATORS
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

void DrawGamepadVisualizer(const DeviceState& dev) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = 400.0f;
    float height = 200.0f;
    ImGui::Dummy(ImVec2(width, height));

    ImU32 color_outline = IM_COL32(200, 200, 200, 255);
    ImU32 color_fill = IM_COL32(100, 100, 100, 255);
    ImU32 color_active = IM_COL32(255, 50, 50, 255);

    // Shoulder Buttons (L1/R1)
    bool l_shoulder_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    bool r_shoulder_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    draw_list->AddRectFilled(ImVec2(p.x + 40, p.y + 20), ImVec2(p.x + 120, p.y + 40), l_shoulder_pressed ? color_active : color_fill, 4.0f);
    draw_list->AddRect(ImVec2(p.x + 40, p.y + 20), ImVec2(p.x + 120, p.y + 40), color_outline, 4.0f);
    draw_list->AddRectFilled(ImVec2(p.x + 280, p.y + 20), ImVec2(p.x + 360, p.y + 40), r_shoulder_pressed ? color_active : color_fill, 4.0f);
    draw_list->AddRect(ImVec2(p.x + 280, p.y + 20), ImVec2(p.x + 360, p.y + 40), color_outline, 4.0f);

    // Triggers (L2/R2)
    Sint16 lt = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    Sint16 rt = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    float lt_norm = (float)lt / 32767.0f;
    float rt_norm = (float)rt / 32767.0f;
    draw_list->AddRect(ImVec2(p.x + 40, p.y + 45), ImVec2(p.x + 120, p.y + 55), color_outline);
    draw_list->AddRectFilled(ImVec2(p.x + 40, p.y + 45), ImVec2(p.x + 40 + 80 * lt_norm, p.y + 55), color_active);
    draw_list->AddRect(ImVec2(p.x + 280, p.y + 45), ImVec2(p.x + 360, p.y + 55), color_outline);
    draw_list->AddRectFilled(ImVec2(p.x + 280, p.y + 45), ImVec2(p.x + 280 + 80 * rt_norm, p.y + 55), color_active);

    // Start & Back/Select Buttons
    bool back_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_BACK);
    bool start_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_START);
    float btn_small_w = 30;
    float btn_small_h = 15;
    ImVec2 back_btn_pos = ImVec2(p.x + width/2 - btn_small_w - 15, p.y + 80);
    ImVec2 start_btn_pos = ImVec2(p.x + width/2 + 15, p.y + 80);
    draw_list->AddRectFilled(back_btn_pos, back_btn_pos + ImVec2(btn_small_w, btn_small_h), back_pressed ? color_active : color_fill, 4.0f);
    draw_list->AddRect(back_btn_pos, back_btn_pos + ImVec2(btn_small_w, btn_small_h), color_outline, 4.0f);
    draw_list->AddRectFilled(start_btn_pos, start_btn_pos + ImVec2(btn_small_w, btn_small_h), start_pressed ? color_active : color_fill, 4.0f);
    draw_list->AddRect(start_btn_pos, start_btn_pos + ImVec2(btn_small_w, btn_small_h), color_outline, 4.0f);

    // Left Stick
    ImVec2 l_stick_center = ImVec2(p.x + 80, p.y + 120);
    float stick_radius = 30.0f;
    bool l_stick_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    draw_list->AddCircle(l_stick_center, stick_radius, l_stick_pressed ? color_active : color_outline);
    Sint16 lx = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    Sint16 ly = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    ImVec2 l_pos = ImVec2(l_stick_center.x + (lx / 32767.0f) * stick_radius, l_stick_center.y + (ly / 32767.0f) * stick_radius);
    draw_list->AddCircleFilled(l_pos, 5.0f, color_active);

    // Right Stick
    ImVec2 r_stick_center = ImVec2(p.x + 320, p.y + 150);
    bool r_stick_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    draw_list->AddCircle(r_stick_center, stick_radius, r_stick_pressed ? color_active : color_outline);
    Sint16 rx = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    Sint16 ry = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
    ImVec2 r_pos = ImVec2(r_stick_center.x + (rx / 32767.0f) * stick_radius, r_stick_center.y + (ry / 32767.0f) * stick_radius);
    draw_list->AddCircleFilled(r_pos, 5.0f, color_active);

    // Buttons (A, B, X, Y)
    ImVec2 btn_center = ImVec2(p.x + 270, p.y + 120);
    float btn_radius = 10.0f;
    
    bool a_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    draw_list->AddCircleFilled(ImVec2(btn_center.x, btn_center.y + 25), btn_radius, a_pressed ? color_active : color_fill);
    bool b_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_EAST);
    draw_list->AddCircleFilled(ImVec2(btn_center.x + 25, btn_center.y), btn_radius, b_pressed ? color_active : color_fill);
    bool x_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_WEST);
    draw_list->AddCircleFilled(ImVec2(btn_center.x - 25, btn_center.y), btn_radius, x_pressed ? color_active : color_fill);
    bool y_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_NORTH);
    draw_list->AddCircleFilled(ImVec2(btn_center.x, btn_center.y - 25), btn_radius, y_pressed ? color_active : color_fill);

    // D-Pad
    ImVec2 dpad_center = ImVec2(p.x + 130, p.y + 150);
    float dpad_size = 15.0f;
    
    bool up = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bool down = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    bool left = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bool right = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

    draw_list->AddRectFilled(ImVec2(dpad_center.x - dpad_size/2, dpad_center.y - dpad_size*1.5), ImVec2(dpad_center.x + dpad_size/2, dpad_center.y - dpad_size/2), up ? color_active : color_fill);
    draw_list->AddRectFilled(ImVec2(dpad_center.x - dpad_size/2, dpad_center.y + dpad_size/2), ImVec2(dpad_center.x + dpad_size/2, dpad_center.y + dpad_size*1.5), down ? color_active : color_fill);
    draw_list->AddRectFilled(ImVec2(dpad_center.x - dpad_size*1.5, dpad_center.y - dpad_size/2), ImVec2(dpad_center.x - dpad_size/2, dpad_center.y + dpad_size/2), left ? color_active : color_fill);
    draw_list->AddRectFilled(ImVec2(dpad_center.x + dpad_size/2, dpad_center.y - dpad_size/2), ImVec2(dpad_center.x + dpad_size*1.5, dpad_center.y + dpad_size/2), right ? color_active : color_fill);
}

void DrawGenericVisualizer(const DeviceState& dev) {
    if (ImGui::BeginTable("DeviceTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Axes");
        ImGui::TableSetupColumn("Buttons");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        for (int i = 0; i < dev.num_axes; i++) {
            Sint16 axis_val = SDL_GetJoystickAxis(dev.joystick, i);
            float norm_val = (float)axis_val / 32767.0f;
            ImGui::Text("Axis %d", i); ImGui::SameLine();
            char label[32]; sprintf(label, "%d", axis_val);
            ImGui::ProgressBar((norm_val + 1.0f) * 0.5f, ImVec2(-1, 0), label);
        }

        ImGui::TableSetColumnIndex(1);
        float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGuiStyle& style = ImGui::GetStyle();
        for (int i = 0; i < dev.num_buttons; i++) {
            bool pressed = SDL_GetJoystickButton(dev.joystick, i);
            if (pressed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::Button(std::to_string(i).c_str(), ImVec2(40, 40));
            if (pressed) ImGui::PopStyleColor();
            float last_button_x2 = ImGui::GetItemRectMax().x;
            float next_button_x2 = last_button_x2 + style.ItemSpacing.x + 40;
            if (i + 1 < dev.num_buttons && next_button_x2 < window_visible_x2) ImGui::SameLine();
        }
        ImGui::EndTable();
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
            if (ImGui::CollapsingHeader((dev.name + (dev.is_gamepad ? " (Gamepad)" : " (Joystick)") + "##" + std::to_string(dev.instance_id)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                if (dev.is_gamepad) DrawGamepadVisualizer(dev);
                else DrawGenericVisualizer(dev);
                
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