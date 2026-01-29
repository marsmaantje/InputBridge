#include "SteeringWheelVisualizer.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include <SDL3/SDL.h>
#include <cmath>

void SteeringWheelVisualizer::Draw(const DeviceState &dev) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = 400.0f;
    float height = 200.0f;
    ImGui::Dummy(ImVec2(width, height));

    ImU32 color_outline = IM_COL32(200, 200, 200, 255);
    ImU32 color_active = IM_COL32(255, 50, 50, 255);

    // Steering Wheel (Axis 0)
    if (dev.num_axes > 0) {
        Sint16 steering = SDL_GetJoystickAxis(dev.joystick, 0);
        // Map -32768..32767 to radians. +/- 270 degrees visual range
        float angle = (float)steering / 32768.0f * (3.14159f * 1.5f);

        ImVec2 center = ImVec2(p.x + 80, p.y + 80);
        float radius = 50.0f;
        float thickness = 8.0f;

        draw_list->AddCircle(center, radius, color_outline, 0, thickness);

        // Draw marker
        ImVec2 marker_end = ImVec2(center.x + sinf(angle) * radius,
                                   center.y - cosf(angle) * radius);
        draw_list->AddLine(center, marker_end, color_active, 4.0f);

        ImGui::SetCursorScreenPos(ImVec2(p.x + 40, p.y + 140));
        ImGui::Text("Steering (Axis 0): %d", steering);
    }

    // Pedals (Axis 1, 2, 3)
    const char *labels[] = {"Throttle", "Brake", "Clutch"};
    for (int i = 0; i < 3; i++) {
        if (i + 1 < dev.num_axes) {
            float x_off = 200.0f + i * 60.0f;
            float y_off = 20.0f;
            float w = 40.0f;
            float h = 100.0f;
            Sint16 val = SDL_GetJoystickAxis(dev.joystick, i + 1);
            float norm = ((float)val + 32768.0f) /
                         65535.0f; // Pedals often rest at -32768
            ImVec2 r_min = ImVec2(p.x + x_off, p.y + y_off);
            ImVec2 r_max = ImVec2(p.x + x_off + w, p.y + y_off + h);
            draw_list->AddRect(r_min, r_max, color_outline);
            float fill_h = h * norm;
            draw_list->AddRectFilled(ImVec2(r_min.x, r_max.y - fill_h), r_max,
                                     color_active);
            ImGui::SetCursorScreenPos(ImVec2(r_min.x, r_max.y + 5));
            ImGui::Text("%s (Axis %d)", labels[i], i + 1);
        }
    }
}
