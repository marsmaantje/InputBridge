#define IMGUI_DEFINE_MATH_OPERATORS
#include "SensorVisualizer.h"
#include "Devices/SensorReader.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

void SensorVisualizer::DrawAxisBar(const char* label, float value, float width) {
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2      pos = ImGui::GetCursorScreenPos();
    float       h   = ImGui::GetFrameHeight();

    // Background track
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h),
                      IM_COL32(60, 60, 60, 200), 3.f);

    // Centre marker
    float cx = pos.x + width * 0.5f;
    dl->AddLine(ImVec2(cx, pos.y + 2), ImVec2(cx, pos.y + h - 2),
                IM_COL32(120, 120, 120, 200));

    // Filled bar from centre
    float filled = value * width * 0.5f;
    float bx0    = (value >= 0.f) ? cx : cx + filled;
    float bx1    = (value >= 0.f) ? cx + filled : cx;
    ImU32 color  = (std::abs(value) > 0.85f)
                    ? IM_COL32(230, 80, 60, 255)
                    : IM_COL32(60, 160, 230, 255);
    if (bx1 > bx0)
        dl->AddRectFilled(ImVec2(bx0, pos.y + 2), ImVec2(bx1, pos.y + h - 2),
                          color, 3.f);

    ImGui::Dummy(ImVec2(width, h));
    ImGui::SameLine();
    ImGui::Text("%-6s %+.3f", label, value);
}

// ── CapSense helpers ──────────────────────────────────────────────────────────

void SensorVisualizer::DrawCapSenseButton(const char* label, bool active) {
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2      pos = ImGui::GetCursorScreenPos();
    ImGuiStyle& sty = ImGui::GetStyle();

    const float pillW = 90.f;
    const float pillH = ImGui::GetFrameHeight();
    const float rounding = pillH * 0.4f;

    // Background pill
    ImU32 bgColor  = active
        ? IM_COL32(255, 50, 50, 255)    // active: red, matching GamepadVisualizer
        : IM_COL32(80,  80, 80, 220);   // inactive: muted grey
    ImU32 outColor = active
        ? IM_COL32(255, 120, 100, 255)
        : IM_COL32(130, 130, 130, 200);

    dl->AddRectFilled(pos, ImVec2(pos.x + pillW, pos.y + pillH), bgColor, rounding);
    dl->AddRect      (pos, ImVec2(pos.x + pillW, pos.y + pillH), outColor, rounding, 0, 1.5f);

    // Centered label text
    ImVec2 textSz = ImGui::CalcTextSize(label);
    ImVec2 textPos = ImVec2(pos.x + (pillW - textSz.x) * 0.5f,
                            pos.y + (pillH - textSz.y) * 0.5f);
    ImU32 textColor = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255);
    dl->AddText(textPos, textColor, label);

    ImGui::Dummy(ImVec2(pillW, pillH));
}

void SensorVisualizer::DrawCapSense(SDL_Gamepad* gamepad) {
    struct CapEntry {
        SDL_GamepadCapSenseType type;
        const char*             label;
    };
    static constexpr CapEntry kCaps[] = {
        { SDL_GAMEPAD_CAPSENSE_LEFT_STICK,  "L Stick"   },
        { SDL_GAMEPAD_CAPSENSE_RIGHT_STICK, "R Stick"   },
        { SDL_GAMEPAD_CAPSENSE_LEFT_GRIP,   "L Grip"    },
        { SDL_GAMEPAD_CAPSENSE_RIGHT_GRIP,  "R Grip"    },
    };

    bool anyPresent = false;
    for (const auto& e : kCaps)
        if (SDL_GamepadHasCapSense(gamepad, e.type)) { anyPresent = true; break; }

    if (!anyPresent) {
        ImGui::TextDisabled("Capacitive-sense inputs not available on this controller.");
        return;
    }

    ImGui::TextDisabled("Touch = 1 (pressed), No touch = 0 (released)");
    ImGui::Spacing();

    ImGui::PushID("capsense");
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    bool firstInRow = true;
    for (const auto& e : kCaps) {
        if (!SDL_GamepadHasCapSense(gamepad, e.type)) continue;
        bool active = SDL_GetGamepadCapSense(gamepad, e.type);

        if (!firstInRow) ImGui::SameLine(0.f, spacing);
        DrawCapSenseButton(e.label, active);
        firstInRow = false;
    }
    ImGui::PopID();
}



void SensorVisualizer::DrawGyro(SDL_Gamepad* gamepad) {
    GyroState g = SensorReader::ReadGyro(gamepad);

    if (!g.available) {
        ImGui::TextDisabled("Gyroscope not available on this controller.");
        return;
    }

    ImGui::TextDisabled("Scale: ±%.0f rad/s → [-1, 1]", GyroState::SCALE);
    ImGui::Spacing();

    ImGui::PushID("gyro");
    DrawAxisBar("X (pitch)", g.x);
    DrawAxisBar("Y (yaw)",   g.y);
    DrawAxisBar("Z (roll)",  g.z);
    ImGui::PopID();
}

// ── Accelerometer ─────────────────────────────────────────────────────────────

void SensorVisualizer::DrawAccel(SDL_Gamepad* gamepad) {
    AccelState a = SensorReader::ReadAccel(gamepad);

    if (!a.available) {
        ImGui::TextDisabled("Accelerometer not available on this controller.");
        return;
    }

    ImGui::TextDisabled("Scale: ±%.0f m/s²  (gravity ≈ ±0.49 at rest)", AccelState::SCALE);
    ImGui::Spacing();

    ImGui::PushID("accel");
    DrawAxisBar("X (lateral)",  a.x);
    DrawAxisBar("Y (vertical)", a.y);
    DrawAxisBar("Z (fore/aft)", a.z);
    ImGui::PopID();
}

// ── Left-side sensors (Joy-Con L / Steam Deck L half) ─────────────────────────

void SensorVisualizer::DrawGyroL(SDL_Gamepad* gamepad) {
    GyroState g = SensorReader::ReadGyroL(gamepad);

    if (!g.available) {
        ImGui::TextDisabled("Left gyroscope not available on this controller.");
        return;
    }

    ImGui::TextDisabled("Scale: ±%.0f rad/s → [-1, 1]", GyroState::SCALE);
    ImGui::Spacing();

    ImGui::PushID("gyro_l");
    DrawAxisBar("X (pitch)", g.x);
    DrawAxisBar("Y (yaw)",   g.y);
    DrawAxisBar("Z (roll)",  g.z);
    ImGui::PopID();
}

void SensorVisualizer::DrawAccelL(SDL_Gamepad* gamepad) {
    AccelState a = SensorReader::ReadAccelL(gamepad);

    if (!a.available) {
        ImGui::TextDisabled("Left accelerometer not available on this controller.");
        return;
    }

    ImGui::TextDisabled("Scale: ±%.0f m/s²  (gravity ≈ ±0.49 at rest)", AccelState::SCALE);
    ImGui::Spacing();

    ImGui::PushID("accel_l");
    DrawAxisBar("X (lateral)",  a.x);
    DrawAxisBar("Y (vertical)", a.y);
    DrawAxisBar("Z (fore/aft)", a.z);
    ImGui::PopID();
}

// ── Right-side sensors (Joy-Con R / Steam Deck R half) ────────────────────────

void SensorVisualizer::DrawGyroR(SDL_Gamepad* gamepad) {
    GyroState g = SensorReader::ReadGyroR(gamepad);

    if (!g.available) {
        ImGui::TextDisabled("Right gyroscope not available on this controller.");
        return;
    }

    ImGui::TextDisabled("Scale: ±%.0f rad/s → [-1, 1]", GyroState::SCALE);
    ImGui::Spacing();

    ImGui::PushID("gyro_r");
    DrawAxisBar("X (pitch)", g.x);
    DrawAxisBar("Y (yaw)",   g.y);
    DrawAxisBar("Z (roll)",  g.z);
    ImGui::PopID();
}

void SensorVisualizer::DrawAccelR(SDL_Gamepad* gamepad) {
    AccelState a = SensorReader::ReadAccelR(gamepad);

    if (!a.available) {
        ImGui::TextDisabled("Right accelerometer not available on this controller.");
        return;
    }

    ImGui::TextDisabled("Scale: ±%.0f m/s²  (gravity ≈ ±0.49 at rest)", AccelState::SCALE);
    ImGui::Spacing();

    ImGui::PushID("accel_r");
    DrawAxisBar("X (lateral)",  a.x);
    DrawAxisBar("Y (vertical)", a.y);
    DrawAxisBar("Z (fore/aft)", a.z);
    ImGui::PopID();
}

// ── Touchpad ──────────────────────────────────────────────────────────────────

void SensorVisualizer::DrawTouch(SDL_Gamepad* gamepad) {
    TouchState t = SensorReader::ReadTouch(gamepad);

    if (!t.available) {
        ImGui::TextDisabled("Touchpad not available on this controller.");
        return;
    }

    // Draw a 2D touchpad surface.
    const float padW = 220.f;
    const float padH = 140.f;
    ImVec2 origin    = ImGui::GetCursorScreenPos();
    ImDrawList* dl   = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin, ImVec2(origin.x + padW, origin.y + padH),
                      IM_COL32(40, 40, 40, 220), 8.f);
    dl->AddRect(origin, ImVec2(origin.x + padW, origin.y + padH),
                IM_COL32(100, 100, 100, 200), 8.f);

    // Crosshair guides
    dl->AddLine(ImVec2(origin.x + padW * 0.5f, origin.y + 4),
                ImVec2(origin.x + padW * 0.5f, origin.y + padH - 4),
                IM_COL32(70, 70, 70, 180));
    dl->AddLine(ImVec2(origin.x + 4, origin.y + padH * 0.5f),
                ImVec2(origin.x + padW - 4, origin.y + padH * 0.5f),
                IM_COL32(70, 70, 70, 180));

    const ImU32 fingerColors[2] = {
        IM_COL32(60, 160, 230, 255),
        IM_COL32(230, 140, 50, 255),
    };

    for (int i = 0; i < 2; ++i) {
        const auto& f = t.fingers[i];
        if (!f.active) continue;

        float fx = origin.x + f.x * padW;
        float fy = origin.y + f.y * padH;
        float r  = 6.f + f.pressure * 8.f;

        dl->AddCircleFilled(ImVec2(fx, fy), r, fingerColors[i] & 0x80FFFFFF); // semi-transparent fill
        dl->AddCircle(ImVec2(fx, fy), r, fingerColors[i]);
    }

    ImGui::Dummy(ImVec2(padW, padH));
    ImGui::Spacing();

    // Numeric readout per finger
    ImGui::PushID("touch");
    for (int i = 0; i < 2; ++i) {
        const auto& f = t.fingers[i];
        ImGui::PushID(i);
        if (f.active) {
            ImGui::Text("Finger %d:  x=%+.3f  y=%+.3f  pressure=%.2f",
                        i, f.x * 2.f - 1.f, f.y * 2.f - 1.f, f.pressure);
        } else {
            ImGui::TextDisabled("Finger %d: (not touching)", i);
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

// ── Main draw ─────────────────────────────────────────────────────────────────

void SensorVisualizer::Draw(const DeviceState& dev) {
    if (!dev.gamepad) {
        ImGui::TextDisabled("Sensor data requires a gamepad handle.");
        return;
    }

    // Re-enable sensors every frame as a last-resort fallback.  The primary
    // recovery path for Steam overlay / focus loss is the
    // SDL_EVENT_WINDOW_FOCUS_GAINED handler in Application::ProcessEvents,
    // which runs before rendering.  This call catches any edge cases where
    // sensors were disabled outside of a normal focus cycle.
    SensorReader::EnableAll(dev.gamepad);

    ImGui::PushID("sensor_viz");

    if (ImGui::CollapsingHeader("Gyroscope", ImGuiTreeNodeFlags_DefaultOpen))
        DrawGyro(dev.gamepad);

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Accelerometer", ImGuiTreeNodeFlags_DefaultOpen))
        DrawAccel(dev.gamepad);

    ImGui::Spacing();

    // ── Split sensors: Joy-Con pair / Steam Deck L+R halves ───────────────────
    const bool hasGyroL  = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_L);
    const bool hasAccelL = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_L);
    const bool hasGyroR  = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_R);
    const bool hasAccelR = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_R);

    if (hasGyroL || hasAccelL) {
        ImGui::Separator();
        ImGui::TextDisabled("Left Joy-Con / L half");
        ImGui::Spacing();

        if (hasGyroL && ImGui::CollapsingHeader("Gyroscope (Left)", ImGuiTreeNodeFlags_DefaultOpen))
            DrawGyroL(dev.gamepad);

        ImGui::Spacing();

        if (hasAccelL && ImGui::CollapsingHeader("Accelerometer (Left)", ImGuiTreeNodeFlags_DefaultOpen))
            DrawAccelL(dev.gamepad);

        ImGui::Spacing();
    }

    if (hasGyroR || hasAccelR) {
        ImGui::Separator();
        ImGui::TextDisabled("Right Joy-Con / R half");
        ImGui::Spacing();

        if (hasGyroR && ImGui::CollapsingHeader("Gyroscope (Right)", ImGuiTreeNodeFlags_DefaultOpen))
            DrawGyroR(dev.gamepad);

        ImGui::Spacing();

        if (hasAccelR && ImGui::CollapsingHeader("Accelerometer (Right)", ImGuiTreeNodeFlags_DefaultOpen))
            DrawAccelR(dev.gamepad);

        ImGui::Spacing();
    }
    // ── End split sensors ─────────────────────────────────────────────────────

    if (ImGui::CollapsingHeader("Touchpad", ImGuiTreeNodeFlags_DefaultOpen))
        DrawTouch(dev.gamepad);

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Capacitive Sense", ImGuiTreeNodeFlags_DefaultOpen))
        DrawCapSense(dev.gamepad);

    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Map sensors to analog channels, and CapSense inputs to digital channels, in the Input Mapper.");
}