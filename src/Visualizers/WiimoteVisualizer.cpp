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
    // The body graphic and the info panel are two side-by-side "columns".
    // Previously this function reserved layout space with a single
    // fixed-size Dummy sized only for the body graphic (height + 20 =
    // 220px), then drew the info panel as free-standing text positioned
    // with SetCursorScreenPos - outside normal layout flow entirely. Text
    // panel height grows with how many buttons/axes are present, so it
    // regularly exceeded that fixed reservation and visually overlapped
    // whatever ImGui laid out next (e.g. the following device's collapsing
    // header). Wrapping the whole widget in one group and reserving space
    // via BeginGroup/EndGroup (which measures actual drawn content) instead
    // of a hardcoded guess fixes that for any button/axis count.
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // Wiimote dimensions (more accurate proportions)
    const float width = 50.0f;
    const float height = 200.0f;

    // Colors
    const ImU32 colBody = IM_COL32(240, 240, 240, 255);
    const ImU32 colOutline = IM_COL32(100, 100, 100, 255);
    const ImU32 colBtnNormal = IM_COL32(200, 200, 200, 255);
    const ImU32 colBtnPressed = IM_COL32(100, 200, 255, 255);  // Blue when pressed

    ImGui::BeginGroup(); // outer group: reserves the widget's REAL total height

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup(); // left column: just the body graphic
    ImGui::Dummy(ImVec2(width + 20, height + 20));
    ImGui::EndGroup();

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

    // Info panel - placed via SameLine() so it participates in normal
    // layout flow (and therefore in the outer group's measured height)
    // instead of being positioned absolutely and independently of it.
    ImGui::SameLine(0.0f, 35.0f);
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
    
    ImGui::EndGroup(); // end info panel column

    ImGui::EndGroup(); // end outer group - reserves max(body, info panel) height
}

// -------------------------------------------------------------------------
// Real raw-HID Wiimote rendering
// -------------------------------------------------------------------------

using namespace InputBridge::Wiimote;

namespace {

const char *ExtensionName(ExtensionType t) {
    switch (t) {
        case ExtensionType::None:                  return "None";
        case ExtensionType::Nunchuk:                return "Nunchuk";
        case ExtensionType::ClassicController:      return "Classic Controller";
        case ExtensionType::ClassicControllerPro:   return "Classic Controller Pro";
        case ExtensionType::BalanceBoard:           return "Balance Board";
        case ExtensionType::GuitarHeroGuitar:       return "Guitar Hero Guitar";
        case ExtensionType::GuitarHeroDrums:        return "Guitar Hero Drums";
        case ExtensionType::MotionPlus:             return "Motion Plus";
        default:                                    return "Unknown";
    }
}

int BatteryBarsCount(BatteryBars b) {
    switch (b) {
        case BatteryBars::Four:  return 4;
        case BatteryBars::Three: return 3;
        case BatteryBars::Two:   return 2;
        case BatteryBars::One:   return 1;
        default:                 return 0;
    }
}

void DrawCoreButtons(const CoreButtons &b) {
    ImGui::Text("Buttons:");
    ImGui::SameLine();
    bool any = false;
    auto btn = [&](bool pressed, const char *name) {
        if (!pressed) return;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", name);
        any = true;
    };
    btn(b.a, "A"); btn(b.b, "B"); btn(b.one, "1"); btn(b.two, "2");
    btn(b.plus, "+"); btn(b.minus, "-"); btn(b.home, "Home");
    btn(b.up, "Up"); btn(b.down, "Down"); btn(b.left, "Left"); btn(b.right, "Right");
    if (!any) { ImGui::SameLine(); ImGui::TextDisabled("(none)"); }
}

void DrawIRPanel(const IRState &ir, bool ir_enabled, bool ir_possibly_hijacked) {
    ImGui::Separator();
    ImGui::Text("IR Camera:");
    ImGui::SameLine();
    if (!ir_enabled) { ImGui::TextDisabled("disabled"); return; }

    if (ir_possibly_hijacked) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "no data - another app may be interfering");
        ImGui::TextDisabled("(commonly Steam Input; see Devices/Wiimote/README.md)");
    } else {
        ImGui::TextDisabled("enabled");
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = 160.0f, h = 120.0f; // mirrors the camera's 128x96-ish FOV aspect
    ImGui::Dummy(ImVec2(w, h));
    dl->AddRect(p, p + ImVec2(w, h), IM_COL32(100, 100, 100, 255));

    for (size_t i = 0; i < ir.size(); ++i) {
        const auto &dot = ir[i];
        if (!dot.visible) continue;
        // Camera reports X in 0-1023, Y in 0-767 (10-bit/~9.5-bit range).
        const float x = p.x + (float(dot.x) / 1023.0f) * w;
        const float y = p.y + (float(dot.y) / 767.0f) * h;
        dl->AddCircleFilled(ImVec2(x, y), 4.0f, IM_COL32(255, 80, 80, 255));
        char label[2] = {char('1' + i), '\0'};
        dl->AddText(ImVec2(x + 5.0f, y - 6.0f), IM_COL32(255, 255, 255, 200), label);
    }

    // Numeric readout of the same 4 slots that get bridged into the mapper
    // as Axis_IR1X/Y..Axis_IR4X/Y (see WiimoteVirtualBridge.cpp) - shown
    // both as raw camera coordinates and as the normalized [-1,1] value the
    // mapper actually sees, so what you bind matches what you're looking at.
    for (size_t i = 0; i < ir.size(); ++i) {
        const auto &dot = ir[i];
        if (dot.visible) {
            const float nx = (float(dot.x) / 1023.0f) * 2.f - 1.f;
            const float ny = (float(dot.y) / 767.0f)  * 2.f - 1.f;
            ImGui::Text("Dot %zu: x=%4u y=%4u  (axis: %.2f, %.2f)",
                        i + 1, dot.x, dot.y, nx, ny);
        } else {
            ImGui::TextDisabled("Dot %zu: not visible  (axis: 0.00, 0.00)", i + 1);
        }
    }
}

void DrawMotionPlus(const MotionPlusState &mp) {
    if (!mp.connected) return;
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Motion Plus");
    if (mp.is_nunchuk_passthrough) {
        ImGui::SameLine();
        ImGui::TextDisabled("(+ Nunchuk passthrough)");
    } else if (mp.is_classic_passthrough) {
        ImGui::SameLine();
        ImGui::TextDisabled("(+ Classic Controller passthrough)");
    }
    ImGui::Text("Gyro (deg/s): Yaw=%.1f Pitch=%.1f Roll=%.1f",
                mp.deg_s_yaw, mp.deg_s_pitch, mp.deg_s_roll);
    ImGui::TextDisabled("  precision: yaw=%s pitch=%s roll=%s",
                         mp.slow_yaw ? "high" : "normal",
                         mp.slow_pitch ? "high" : "normal",
                         mp.slow_roll ? "high" : "normal");
}

void DrawNunchuk(const NunchukState &n) {
    if (!n.connected) return;
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Nunchuk");
    ImGui::Text("Stick: %d, %d", n.stick_x, n.stick_y);
    ImGui::Text("Accel: %u, %u, %u", n.accel_x, n.accel_y, n.accel_z);
    ImGui::Text("C: %s   Z: %s", n.button_c ? "pressed" : "-", n.button_z ? "pressed" : "-");
}

void DrawClassic(const ClassicControllerState &c) {
    if (!c.connected) return;
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", c.is_pro ? "Classic Controller Pro" : "Classic Controller");
    ImGui::Text("Left stick: %u, %u   Right stick: %u, %u", c.left_x, c.left_y, c.right_x, c.right_y);
    ImGui::Text("Triggers: L=%u R=%u", c.left_trigger, c.right_trigger);
    ImGui::Text("Buttons:");
    ImGui::SameLine();
    bool any = false;
    auto btn = [&](bool pressed, const char *name) {
        if (!pressed) return;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", name);
        any = true;
    };
    btn(c.a, "A"); btn(c.b, "B"); btn(c.x, "X"); btn(c.y, "Y");
    btn(c.l, "L"); btn(c.r, "R"); btn(c.zl, "ZL"); btn(c.zr, "ZR");
    btn(c.plus, "+"); btn(c.minus, "-"); btn(c.home, "Home");
    if (!any) { ImGui::SameLine(); ImGui::TextDisabled("(none)"); }
}

void DrawGuitar(const GuitarHeroState &g) {
    if (!g.connected) return;
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", g.is_drums ? "Guitar Hero Drums" : "Guitar Hero Guitar");
    ImGui::TextDisabled("(fret/strum mapping unverified on real hardware - see README)");
    ImGui::Text("Frets:");
    ImGui::SameLine();
    auto fret = [&](bool on, const char *name, ImVec4 col) {
        ImGui::SameLine();
        if (on) ImGui::TextColored(col, "%s", name); else ImGui::TextDisabled("%s", name);
    };
    fret(g.fret_green,  "GREEN",  ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    fret(g.fret_red,    "RED",    ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    fret(g.fret_yellow, "YELLOW", ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
    fret(g.fret_blue,   "BLUE",   ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
    fret(g.fret_orange, "ORANGE", ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
    ImGui::Text("Whammy: %u   Strum: %s%s", g.whammy_bar,
                g.strum_up ? "Up " : "", g.strum_down ? "Down" : "");
}

void DrawBalanceBoard(const BalanceBoardState &bb, bool recovering, int recovery_attempts, int index) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Balance Board");
    ImGui::Text("Total weight: %.1f kg", bb.kg_total);
    ImGui::Text("  TL: %.1f  TR: %.1f", bb.kg_top_left, bb.kg_top_right);
    ImGui::Text("  BL: %.1f  BR: %.1f", bb.kg_bottom_left, bb.kg_bottom_right);
    ImGui::Text("Center of gravity: %.2f, %.2f", bb.cog_x, bb.cog_y);
    ImGui::Text("Button: %s", bb.button_a ? "pressed" : "-");

    if (recovering) {
        // See WiimoteDevice::CheckBalanceBoardStuckSensors - this is the
        // known WiiBrew-documented "one or more sensors disabled" quirk.
        if (recovery_attempts >= 4) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                "Some sensors still stuck after %d retries - try removing "
                "the batteries for ~30s and reconnecting.",
                recovery_attempts);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "Some sensors look stuck - retrying (%d)...",
                recovery_attempts);
        }
    }

    // Dot-size mode. Relative mode (fraction of total per corner) flickers
    // wildly when nobody's on the board: at near-zero total weight, tiny
    // sensor noise on each of the 4 corners dominates the ratio, so dot
    // radii swing between "smallest" and "largest" from frame to frame even
    // though nothing physically changed. Absolute mode (each corner scaled
    // against a fixed kg reference, independent of the others) doesn't have
    // that failure mode, so it's the default; Relative is opt-in for anyone
    // who specifically wants to see weight *distribution* rather than
    // *magnitude*, and gets a deadband below to tame (not fully eliminate -
    // it's inherent to ratios of small numbers) its own flicker.
    static bool s_absolute_mode[8] = { true, true, true, true, true, true, true, true };
    bool &absolute_mode = s_absolute_mode[index % 8];

    ImGui::Text("Dot size:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Absolute (kg)", absolute_mode)) absolute_mode = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Relative (%)", !absolute_mode)) absolute_mode = false;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float size = 100.0f;
    ImGui::Dummy(ImVec2(size, size));
    dl->AddRect(p, p + ImVec2(size, size), IM_COL32(100, 100, 100, 255));

    // Per-corner reference max for absolute mode. A board tops out around
    // ~135kg total; a single corner realistically maxes out well below
    // that (weight shifts, it doesn't fully concentrate on one sensor even
    // when leaning), so 50kg/corner gives reasonable visual range without
    // every normal standing position pinning all four dots to full size.
    constexpr float kAbsoluteRefKg = 50.0f;
    // Below this total, relative mode's ratio is dominated by sensor noise
    // rather than real weight distribution - collapse to minimum dot size
    // instead of showing meaningless (and flickering) proportions.
    constexpr float kRelativeMinTotalKg = 2.0f;

    auto corner = [&](float fx, float fy, float kg) {
        float t;
        if (absolute_mode) {
            t = std::clamp(kg / kAbsoluteRefKg, 0.f, 1.f);
        } else {
            t = (bb.kg_total > kRelativeMinTotalKg)
                    ? std::clamp(kg / bb.kg_total, 0.f, 1.f)
                    : 0.f;
        }
        const float r = 4.0f + t * 14.0f;
        dl->AddCircleFilled(p + ImVec2(fx * size, fy * size), r, IM_COL32(80, 160, 255, 220));
    };
    corner(0.2f, 0.2f, bb.kg_top_left);
    corner(0.8f, 0.2f, bb.kg_top_right);
    corner(0.2f, 0.8f, bb.kg_bottom_left);
    corner(0.8f, 0.8f, bb.kg_bottom_right);
}

} // namespace

void WiimoteVisualizer::Draw(const WiimoteSnapshot &snap, int index) {
    ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "%s",
                        snap.is_balance_board ? "Wii Balance Board" : "Wii Remote");
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", snap.connected ? "connected" : "no data yet");

    ImGui::Text("Battery:");
    ImGui::SameLine();
    for (int i = 0; i < 4; ++i) {
        ImGui::TextColored(i < BatteryBarsCount(snap.battery)
                                ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
                            "|");
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (snap.is_balance_board) {
        DrawBalanceBoard(snap.balance_board, snap.balance_board_recovering,
                          snap.balance_board_recovery_attempts, index);
        return;
    }

    DrawCoreButtons(snap.core);

    ImGui::Separator();
    ImGui::Text("Accel (g): X=%.2f Y=%.2f Z=%.2f", snap.accel.g_x, snap.accel.g_y, snap.accel.g_z);

    DrawIRPanel(snap.ir, snap.ir_enabled, snap.ir_possibly_hijacked);

    if (snap.motion_plus.connected) {
        DrawMotionPlus(snap.motion_plus);
    }

    ImGui::Separator();
    ImGui::Text("Extension: %s", ExtensionName(snap.extension));
    // When Motion Plus is active it occupies the extension byte slot itself
    // (see WiimoteDevice::DecodeCoreAccelIR10Ext6), so its passthrough
    // Nunchuk/Classic Controller data isn't separately decoded yet - avoid
    // drawing a stale/zeroed Nunchuk or Classic panel underneath it.
    if (!snap.motion_plus.connected) {
        switch (snap.extension) {
            case ExtensionType::Nunchuk:              DrawNunchuk(snap.nunchuk); break;
            case ExtensionType::ClassicController:
            case ExtensionType::ClassicControllerPro: DrawClassic(snap.classic); break;
            case ExtensionType::GuitarHeroGuitar:
            case ExtensionType::GuitarHeroDrums:      DrawGuitar(snap.guitar); break;
            default: break;
        }
    }
}