#include "GamepadVisualizer.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include <SDL3/SDL.h>

void GamepadVisualizer::Draw(const DeviceState& dev) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = 400.0f;
    float height = 200.0f;
    ImGui::Dummy(ImVec2(width, height));

    ImU32 color_outline = IM_COL32(200, 200, 200, 255);
    ImU32 color_fill = IM_COL32(100, 100, 100, 255);
    ImU32 color_active = IM_COL32(255, 50, 50, 255);

    // Shoulder Buttons (L1/R1)
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        bool l_shoulder_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        draw_list->AddRectFilled(ImVec2(p.x + 40, p.y + 20), ImVec2(p.x + 120, p.y + 40), l_shoulder_pressed ? color_active : color_fill, 4.0f);
        draw_list->AddRect(ImVec2(p.x + 40, p.y + 20), ImVec2(p.x + 120, p.y + 40), color_outline, 4.0f);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        bool r_shoulder_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        draw_list->AddRectFilled(ImVec2(p.x + 280, p.y + 20), ImVec2(p.x + 360, p.y + 40), r_shoulder_pressed ? color_active : color_fill, 4.0f);
        draw_list->AddRect(ImVec2(p.x + 280, p.y + 20), ImVec2(p.x + 360, p.y + 40), color_outline, 4.0f);
    }

    // Triggers (L2/R2)
    if (SDL_GamepadHasAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) {
        Sint16 lt = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        float lt_norm = (float)lt / 32767.0f;
        draw_list->AddRect(ImVec2(p.x + 40, p.y + 45), ImVec2(p.x + 120, p.y + 55), color_outline);
        draw_list->AddRectFilled(ImVec2(p.x + 40, p.y + 45), ImVec2(p.x + 40 + 80 * lt_norm, p.y + 55), color_active);
    }
    if (SDL_GamepadHasAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        Sint16 rt = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        float rt_norm = (float)rt / 32767.0f;
        draw_list->AddRect(ImVec2(p.x + 280, p.y + 45), ImVec2(p.x + 360, p.y + 55), color_outline);
        draw_list->AddRectFilled(ImVec2(p.x + 280, p.y + 45), ImVec2(p.x + 280 + 80 * rt_norm, p.y + 55), color_active);
    }

    // Start & Back/Select Buttons
    float btn_small_w = 30;
    float btn_small_h = 15;
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_BACK)) {
        bool back_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_BACK);
        ImVec2 back_btn_pos = ImVec2(p.x + width/2 - btn_small_w - 15, p.y + 80);
        draw_list->AddRectFilled(back_btn_pos, back_btn_pos + ImVec2(btn_small_w, btn_small_h), back_pressed ? color_active : color_fill, 4.0f);
        draw_list->AddRect(back_btn_pos, back_btn_pos + ImVec2(btn_small_w, btn_small_h), color_outline, 4.0f);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_START)) {
        bool start_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_START);
        ImVec2 start_btn_pos = ImVec2(p.x + width/2 + 15, p.y + 80);
        draw_list->AddRectFilled(start_btn_pos, start_btn_pos + ImVec2(btn_small_w, btn_small_h), start_pressed ? color_active : color_fill, 4.0f);
        draw_list->AddRect(start_btn_pos, start_btn_pos + ImVec2(btn_small_w, btn_small_h), color_outline, 4.0f);
    }

    // Left Stick
    if (SDL_GamepadHasAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFTX) || SDL_GamepadHasAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFTY)) {
        ImVec2 l_stick_center = ImVec2(p.x + 80, p.y + 120);
        float stick_radius = 30.0f;
        bool l_stick_pressed = SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) && SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
        draw_list->AddCircle(l_stick_center, stick_radius, l_stick_pressed ? color_active : color_outline);
        Sint16 lx = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 ly = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        ImVec2 l_pos = ImVec2(l_stick_center.x + (lx / 32767.0f) * stick_radius, l_stick_center.y + (ly / 32767.0f) * stick_radius);
        draw_list->AddCircleFilled(l_pos, 5.0f, color_active);
    }

    // Right Stick
    if (SDL_GamepadHasAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHTX) || SDL_GamepadHasAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHTY)) {
        ImVec2 r_stick_center = ImVec2(p.x + 320, p.y + 150);
        float stick_radius = 30.0f;
        bool r_stick_pressed = SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK) && SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        draw_list->AddCircle(r_stick_center, stick_radius, r_stick_pressed ? color_active : color_outline);
        Sint16 rx = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        Sint16 ry = SDL_GetGamepadAxis(dev.gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        ImVec2 r_pos = ImVec2(r_stick_center.x + (rx / 32767.0f) * stick_radius, r_stick_center.y + (ry / 32767.0f) * stick_radius);
        draw_list->AddCircleFilled(r_pos, 5.0f, color_active);
    }

    // Buttons (A, B, X, Y)
    ImVec2 btn_center = ImVec2(p.x + 270, p.y + 120);
    float btn_radius = 10.0f;
    
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) {
        bool a_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        draw_list->AddCircleFilled(ImVec2(btn_center.x, btn_center.y + 25), btn_radius, a_pressed ? color_active : color_fill);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_EAST)) {
        bool b_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_EAST);
        draw_list->AddCircleFilled(ImVec2(btn_center.x + 25, btn_center.y), btn_radius, b_pressed ? color_active : color_fill);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_WEST)) {
        bool x_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_WEST);
        draw_list->AddCircleFilled(ImVec2(btn_center.x - 25, btn_center.y), btn_radius, x_pressed ? color_active : color_fill);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_NORTH)) {
        bool y_pressed = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_NORTH);
        draw_list->AddCircleFilled(ImVec2(btn_center.x, btn_center.y - 25), btn_radius, y_pressed ? color_active : color_fill);
    }

    // D-Pad
    ImVec2 dpad_center = ImVec2(p.x + 130, p.y + 150);
    float dpad_size = 15.0f;
    
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
        bool up = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        draw_list->AddRectFilled(ImVec2(dpad_center.x - dpad_size/2, dpad_center.y - dpad_size*1.5), ImVec2(dpad_center.x + dpad_size/2, dpad_center.y - dpad_size/2), up ? color_active : color_fill);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
        bool down = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        draw_list->AddRectFilled(ImVec2(dpad_center.x - dpad_size/2, dpad_center.y + dpad_size/2), ImVec2(dpad_center.x + dpad_size/2, dpad_center.y + dpad_size*1.5), down ? color_active : color_fill);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
        bool left = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        draw_list->AddRectFilled(ImVec2(dpad_center.x - dpad_size*1.5, dpad_center.y - dpad_size/2), ImVec2(dpad_center.x - dpad_size/2, dpad_center.y + dpad_size/2), left ? color_active : color_fill);
    }
    if (SDL_GamepadHasButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
        bool right = SDL_GetGamepadButton(dev.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        draw_list->AddRectFilled(ImVec2(dpad_center.x + dpad_size/2, dpad_center.y - dpad_size/2), ImVec2(dpad_center.x + dpad_size*1.5, dpad_center.y + dpad_size/2), right ? color_active : color_fill);
    }
}
