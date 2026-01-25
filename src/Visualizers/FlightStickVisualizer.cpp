#include "FlightStickVisualizer.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include <SDL3/SDL.h>

void FlightStickVisualizer::Draw(const DeviceState& dev) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = 400.0f;
    float height = 250.0f;
    ImGui::Dummy(ImVec2(width, height));

    ImU32 color_outline = IM_COL32(200, 200, 200, 255);
    ImU32 color_active = IM_COL32(255, 50, 50, 255);

    // Stick (Axis 0: Roll, Axis 1: Pitch)
    if (dev.num_axes >= 2) {
        ImVec2 stick_center = ImVec2(p.x + 100, p.y + 100);
        float stick_radius = 80.0f;
        draw_list->AddCircle(stick_center, stick_radius, color_outline);
        draw_list->AddLine(ImVec2(stick_center.x - stick_radius, stick_center.y), ImVec2(stick_center.x + stick_radius, stick_center.y), IM_COL32(150, 150, 150, 100));
        draw_list->AddLine(ImVec2(stick_center.x, stick_center.y - stick_radius), ImVec2(stick_center.x, stick_center.y + stick_radius), IM_COL32(150, 150, 150, 100));

        Sint16 x = SDL_GetJoystickAxis(dev.joystick, 0);
        Sint16 y = SDL_GetJoystickAxis(dev.joystick, 1);
        
        float x_norm = (float)x / 32767.0f;
        float y_norm = (float)y / 32767.0f;

        ImVec2 pos = stick_center + ImVec2(x_norm, y_norm) * stick_radius;
        draw_list->AddCircleFilled(pos, 10.0f, color_active);
        
        ImGui::SetCursorScreenPos(ImVec2(p.x + 20, p.y + 190));
        ImGui::Text("Stick X: %d", x);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 20, p.y + 205));
        ImGui::Text("Stick Y: %d", y);
    }

    // Throttle (Axis 2)
    if (dev.num_axes > 2) {
        float x_off = 220.0f;
        float y_off = 20.0f;
        float w = 30.0f;
        float h = 160.0f;
        
        Sint16 val = SDL_GetJoystickAxis(dev.joystick, 2);
        // Throttle often -32768..32767. We map -32768 to 0% (bottom) and 32767 to 100% (top)
        float norm = ((float)val + 32768.0f) / 65535.0f; 
        
        ImVec2 r_min = ImVec2(p.x + x_off, p.y + y_off);
        ImVec2 r_max = ImVec2(p.x + x_off + w, p.y + y_off + h);
        
        draw_list->AddRect(r_min, r_max, color_outline);
        float fill_h = h * norm;
        // Fill from bottom up
        draw_list->AddRectFilled(ImVec2(r_min.x, r_max.y - fill_h), r_max, color_active);
        
        ImGui::SetCursorScreenPos(ImVec2(r_min.x, r_max.y + 5));
        ImGui::Text("Throttle");
    }

    // Rudder/Twist (Axis 3)
    if (dev.num_axes > 3) {
        float x_off = 280.0f;
        float y_off = 20.0f;
        float w = 30.0f;
        float h = 160.0f;

        Sint16 val = SDL_GetJoystickAxis(dev.joystick, 3);
        float norm = (float)val / 32767.0f; // -1 to 1

        ImVec2 center = ImVec2(p.x + x_off + w/2, p.y + y_off + h/2);
        draw_list->AddRect(ImVec2(p.x + x_off, p.y + y_off), ImVec2(p.x + x_off + w, p.y + y_off + h), color_outline);
        
        // Draw a bar from center
        float bar_h = (h/2) * norm;
        if (bar_h > 0)
            draw_list->AddRectFilled(ImVec2(p.x + x_off, center.y), ImVec2(p.x + x_off + w, center.y + bar_h), color_active);
        else
            draw_list->AddRectFilled(ImVec2(p.x + x_off, center.y + bar_h), ImVec2(p.x + x_off + w, center.y), color_active);

        ImGui::SetCursorScreenPos(ImVec2(p.x + x_off, p.y + y_off + h + 5));
        ImGui::Text("Rudder");
    }
    
    // Hats (POV)
    if (dev.num_hats > 0) {
        ImVec2 hat_center = ImVec2(p.x + 360, p.y + 60);
        float hat_radius = 20.0f;
        draw_list->AddCircle(hat_center, hat_radius, color_outline);
        
        Uint8 hat = SDL_GetJoystickHat(dev.joystick, 0);
        if (hat != SDL_HAT_CENTERED) {
            ImVec2 dir(0,0);
            if (hat & SDL_HAT_UP) dir.y -= 1.0f;
            if (hat & SDL_HAT_DOWN) dir.y += 1.0f;
            if (hat & SDL_HAT_LEFT) dir.x -= 1.0f;
            if (hat & SDL_HAT_RIGHT) dir.x += 1.0f;
            
            ImVec2 pos = hat_center + dir * (hat_radius * 0.7f);
            draw_list->AddCircleFilled(pos, 5.0f, color_active);
        }
        ImGui::SetCursorScreenPos(ImVec2(hat_center.x - 15, hat_center.y + 25));
        ImGui::Text("Hat");
    }
}
