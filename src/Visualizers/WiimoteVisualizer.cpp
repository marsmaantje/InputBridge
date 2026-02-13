// src/Visualizers/WiimoteVisualizer.cpp
#include "WiimoteVisualizer.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include <SDL3/SDL.h>

void WiimoteVisualizer::Draw(const DeviceState &dev) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    
    // Wiimote dimensions
    float w = 60.0f;
    float h = 220.0f;
    
    ImGui::Dummy(ImVec2(w + 150, h + 20));

    ImU32 col_body = IM_COL32(240, 240, 240, 255);
    ImU32 col_outline = IM_COL32(180, 180, 180, 255);
    ImU32 col_btn = IM_COL32(200, 200, 200, 255);

    ImVec2 center = p + ImVec2(w/2 + 20, h/2 + 10);
    ImVec2 top_left = center - ImVec2(w/2, h/2);
    ImVec2 bottom_right = center + ImVec2(w/2, h/2);

    // Body
    draw_list->AddRectFilled(top_left, bottom_right, col_body, 10.0f);
    draw_list->AddRect(top_left, bottom_right, col_outline, 10.0f);

    // D-Pad Area (Top)
    ImVec2 dpad_pos = top_left + ImVec2(w/2, 40);
    draw_list->AddRectFilled(dpad_pos - ImVec2(12, 4), dpad_pos + ImVec2(12, 4), col_btn);
    draw_list->AddRectFilled(dpad_pos - ImVec2(4, 12), dpad_pos + ImVec2(4, 12), col_btn);

    // A Button (Large)
    ImVec2 a_pos = top_left + ImVec2(w/2, 75);
    draw_list->AddCircleFilled(a_pos, 12.0f, col_btn);
    draw_list->AddCircle(a_pos, 12.0f, col_outline);

    // -, Home, + Buttons (Middle)
    float mid_y = top_left.y + h/2;
    draw_list->AddCircleFilled(ImVec2(top_left.x + w/2 - 15, mid_y), 6.0f, col_btn); // -
    draw_list->AddCircleFilled(ImVec2(top_left.x + w/2 + 15, mid_y), 6.0f, col_btn); // +
    draw_list->AddCircleFilled(ImVec2(top_left.x + w/2, mid_y), 6.0f, col_btn);      // Home

    // 1 & 2 Buttons (Bottom)
    ImVec2 one_pos = bottom_right - ImVec2(w/2, 55);
    ImVec2 two_pos = bottom_right - ImVec2(w/2, 25);
    draw_list->AddCircleFilled(one_pos, 8.0f, col_btn);
    draw_list->AddCircleFilled(two_pos, 8.0f, col_btn);

    // Info
    ImGui::SetCursorScreenPos(p + ImVec2(w + 40, 10));
    ImGui::BeginGroup();
    ImGui::Text("Wiimote Detected");
    ImGui::Text("Axes: %d", dev.num_axes);
    ImGui::Text("Buttons: %d", dev.num_buttons);
    
    // Visualize buttons if pressed
    for (int i = 0; i < dev.num_buttons; ++i) {
        if (SDL_GetJoystickButton(dev.joystick, i)) {
            ImGui::TextColored(ImVec4(1,0,0,1), "Button %d", i);
        }
    }
    ImGui::EndGroup();
}
