#pragma once
#include "DeviceVisualizer.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <string>

class GenericVisualizer : public DeviceVisualizer {
public:
    void Draw(const DeviceState& dev) override {
        if (ImGui::BeginTable("DeviceTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Axes");
            ImGui::TableSetupColumn("Buttons");
            ImGui::TableSetupColumn("Hats");
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
            float visible_x2 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGuiStyle& style = ImGui::GetStyle();
            for (int i = 0; i < dev.num_buttons; i++) {
                bool pressed = SDL_GetJoystickButton(dev.joystick, i);
                if (pressed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                ImGui::Button(std::to_string(i).c_str(), ImVec2(40, 40));
                if (pressed) ImGui::PopStyleColor();
                float last_button_x2 = ImGui::GetItemRectMax().x;
                float next_button_x2 = last_button_x2 + style.ItemSpacing.x + 40;
                if (i + 1 < dev.num_buttons && next_button_x2 < visible_x2) ImGui::SameLine();
            }

            ImGui::TableSetColumnIndex(2);
            for (int i = 0; i < dev.num_hats; i++) {
                Uint8 hat = SDL_GetJoystickHat(dev.joystick, i);
                std::string hat_str;
                if (hat == SDL_HAT_CENTERED) hat_str = "CENTER";
                else {
                    if (hat & SDL_HAT_UP) hat_str += "UP ";
                    if (hat & SDL_HAT_DOWN) hat_str += "DOWN ";
                    if (hat & SDL_HAT_LEFT) hat_str += "LEFT ";
                    if (hat & SDL_HAT_RIGHT) hat_str += "RIGHT ";
                }
                ImGui::Text("Hat %d: %s", i, hat_str.c_str());
            }
            ImGui::EndTable();
        }
    }
};