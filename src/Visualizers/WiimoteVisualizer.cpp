/**
 * @file WiimoteVisualizer.cpp (Enhanced)
 * @brief Improved Wiimote controller visualization
 * 
 * Enhancements:
 * - Better button highlighting with colors
 * - Proper button labels
 * - Axis value display
 * - More accurate Wiimote proportions
 * 
 * @author InputBridge Team
 * @version 2.0
 * @date 2026-02-14
 */

#include "WiimoteVisualizer.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include <SDL3/SDL.h>
#include <algorithm>

void WiimoteVisualizer::Draw(const DeviceState &dev) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    
    // Wiimote dimensions (more accurate proportions)
    const float width = 50.0f;
    const float height = 200.0f;
    
    ImGui::Dummy(ImVec2(width + 180, height + 20));

    // Colors
    const ImU32 colBody = IM_COL32(240, 240, 240, 255);
    const ImU32 colOutline = IM_COL32(100, 100, 100, 255);
    const ImU32 colBtnNormal = IM_COL32(200, 200, 200, 255);
    const ImU32 colBtnPressed = IM_COL32(100, 200, 255, 255);  // Blue when pressed

    const ImVec2 center = p + ImVec2(width / 2 + 20, height / 2 + 10);
    const ImVec2 topLeft = center - ImVec2(width / 2, height / 2);
    const ImVec2 bottomRight = center + ImVec2(width / 2, height / 2);

    // Draw Wiimote body
    drawList->AddRectFilled(topLeft, bottomRight, colBody, 8.0f);
    drawList->AddRect(topLeft, bottomRight, colOutline, 8.0f, 0, 2.0f);

    // Helper lambda to check if button is pressed
    auto isPressed = [&dev](int buttonIndex) -> bool {
        if (buttonIndex >= 0 && buttonIndex < dev.num_buttons) {
            return SDL_GetJoystickButton(dev.joystick, buttonIndex);
        }
        return false;
    };

    // D-Pad (top)
    const ImVec2 dpadPos = topLeft + ImVec2(width / 2, 35);
    const float dpadSize = 12.0f;
    
    // D-Pad cross shape
    const ImU32 dpadCol = colBtnNormal;
    drawList->AddRectFilled(
        dpadPos + ImVec2(-3, -dpadSize),
        dpadPos + ImVec2(3, dpadSize),
        dpadCol
    );
    drawList->AddRectFilled(
        dpadPos + ImVec2(-dpadSize, -3),
        dpadPos + ImVec2(dpadSize, 3),
        dpadCol
    );

    // A Button (large, center)
    const ImVec2 aPos = topLeft + ImVec2(width / 2, 70);
    const ImU32 aCol = isPressed(0) ? colBtnPressed : colBtnNormal;
    drawList->AddCircleFilled(aPos, 11.0f, aCol);
    drawList->AddCircle(aPos, 11.0f, colOutline, 0, 1.5f);

    // -, HOME, + Buttons (middle section)
    const float midY = topLeft.y + height / 2;
    
    // Minus button
    const ImVec2 minusPos = ImVec2(topLeft.x + width / 2 - 14, midY);
    const ImU32 minusCol = isPressed(1) ? colBtnPressed : colBtnNormal;
    drawList->AddCircleFilled(minusPos, 5.0f, minusCol);
    drawList->AddCircle(minusPos, 5.0f, colOutline);
    
    // Home button
    const ImVec2 homePos = ImVec2(topLeft.x + width / 2, midY);
    const ImU32 homeCol = isPressed(2) ? colBtnPressed : colBtnNormal;
    drawList->AddCircleFilled(homePos, 5.0f, homeCol);
    drawList->AddCircle(homePos, 5.0f, colOutline);
    
    // Plus button
    const ImVec2 plusPos = ImVec2(topLeft.x + width / 2 + 14, midY);
    const ImU32 plusCol = isPressed(3) ? colBtnPressed : colBtnNormal;
    drawList->AddCircleFilled(plusPos, 5.0f, plusCol);
    drawList->AddCircle(plusPos, 5.0f, colOutline);

    // 1 & 2 Buttons (bottom section)
    const ImVec2 onePos = bottomRight - ImVec2(width / 2, 50);
    const ImU32 oneCol = isPressed(4) ? colBtnPressed : colBtnNormal;
    drawList->AddCircleFilled(onePos, 7.0f, oneCol);
    drawList->AddCircle(onePos, 7.0f, colOutline);
    
    const ImVec2 twoPos = bottomRight - ImVec2(width / 2, 25);
    const ImU32 twoCol = isPressed(5) ? colBtnPressed : colBtnNormal;
    drawList->AddCircleFilled(twoPos, 7.0f, twoCol);
    drawList->AddCircle(twoPos, 7.0f, colOutline);

    // Info panel
    ImGui::SetCursorScreenPos(p + ImVec2(width + 35, 10));
    ImGui::BeginGroup();
    
    ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Wiimote");
    ImGui::Separator();
    
    ImGui::Text("Buttons: %d", dev.num_buttons);
    ImGui::Text("Axes: %d", dev.num_axes);
    
    // Display axis values if available
    if (dev.num_axes > 0) {
        ImGui::Separator();
        ImGui::Text("Accelerometer:");
        for (int i = 0; i < std::min(dev.num_axes, 3); i++) {
            const float value = SDL_GetJoystickAxis(dev.joystick, i) / 32767.0f;
            const char* axisName[] = {"X", "Y", "Z"};
            ImGui::Text("  %s: %.2f", axisName[i], value);
        }
    }
    
    // Display pressed buttons
    ImGui::Separator();
    ImGui::Text("Pressed:");
    bool anyPressed = false;
    for (int i = 0; i < dev.num_buttons; i++) {
        if (SDL_GetJoystickButton(dev.joystick, i)) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "  Button %d", i);
            anyPressed = true;
        }
    }
    if (!anyPressed) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  (none)");
    }
    
    ImGui::EndGroup();
}
