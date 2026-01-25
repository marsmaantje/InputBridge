#include "GenericVisualizer.h"
#include "imgui.h"
#include <SDL3/SDL.h>

void GenericVisualizer::Draw(const DeviceState& dev) {
    ImGui::Text("Generic Device Visualizer");
    
    if (dev.joystick) {
        ImGui::Text("Name: %s", SDL_GetJoystickName(dev.joystick));
        
        if (ImGui::CollapsingHeader("Axes", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < dev.num_axes; ++i) {
                Sint16 val = SDL_GetJoystickAxis(dev.joystick, i);
                float norm = (float)val / 32767.0f;
                ImGui::Text("Axis %d: %d", i, val);
                ImGui::SameLine();
                ImGui::ProgressBar((norm + 1.0f) * 0.5f, ImVec2(-1, 0), "");
            }
        }

        if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < dev.num_buttons; ++i) {
                if (i > 0 && i % 8 != 0) ImGui::SameLine();
                bool pressed = SDL_GetJoystickButton(dev.joystick, i);
                ImGui::TextColored(pressed ? ImVec4(0, 1, 0, 1) : ImVec4(0.5, 0.5, 0.5, 1), "B%d", i);
            }
        }

        if (ImGui::CollapsingHeader("Hats", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = 0; i < dev.num_hats; ++i) {
                Uint8 hat = SDL_GetJoystickHat(dev.joystick, i);
                const char* dir = "CENTER";
                if (hat & SDL_HAT_UP) dir = "UP";
                if (hat & SDL_HAT_DOWN) dir = "DOWN";
                if (hat & SDL_HAT_LEFT) dir = "LEFT";
                if (hat & SDL_HAT_RIGHT) dir = "RIGHT";
                if (hat == (SDL_HAT_RIGHT | SDL_HAT_UP)) dir = "UP-RIGHT";
                if (hat == (SDL_HAT_RIGHT | SDL_HAT_DOWN)) dir = "DOWN-RIGHT";
                if (hat == (SDL_HAT_LEFT | SDL_HAT_UP)) dir = "UP-LEFT";
                if (hat == (SDL_HAT_LEFT | SDL_HAT_DOWN)) dir = "DOWN-LEFT";
                
                ImGui::Text("Hat %d: %s (%d)", i, dir, hat);
            }
        }
    }
}