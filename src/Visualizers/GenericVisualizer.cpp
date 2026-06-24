#include "GenericVisualizer.h"
#include "UI/InputLabelProvider.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <SDL3/SDL.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Renders a small Kenney icon inline at text height, then SameLine()s so the
// caller can immediately print the label text.  Does nothing when the icon is
// not valid, so the caller's layout is unaffected either way.
void DrawInlineIcon(const DeviceIcon& icon)
{
    if (!icon.IsValid()) return;

    const float textH = ImGui::GetTextLineHeight();

    // Kenney pictogram glyphs occupy only ~30-50% of their em square, with a
    // large gap above and below, so we scale the *visible* pixel height up to
    // match the text line — but never beyond it, or the icon cell ends up
    // wider than the row is tall (this is the same approach DevicePanel.cpp
    // uses for the device-header icon).
    const float bakeSize   = ImGui::GetStyle().FontSizeBase * 4.0f;
    float       renderSize = textH; // fallback: render at 1:1 em
    float       y0_scaled  = 0.0f;
    float       glyphH_scaled = textH;

    if (ImFontBaked* baked = icon.font->GetFontBaked(bakeSize))
    {
        const ImFontGlyph* g = baked->FindGlyphNoFallback(icon.codepoint);
        if (g && baked->Size > 0.0f)
        {
            const float glyphH = g->Y1 - g->Y0;
            const float fill   = glyphH / baked->Size;
            if (fill > 0.01f)
            {
                renderSize    = std::min(textH / fill, textH * 2.5f); // clamp: never balloon the cell
                y0_scaled     = (g->Y0 / baked->Size) * renderSize;
                glyphH_scaled = (glyphH / baked->Size) * renderSize;
            }
        }
    }

    // Reserve a square cell exactly one text-line tall, regardless of the
    // glyph's em-square size, so the icon column stays a consistent width
    // and lines up with the row instead of overflowing it.
    const float cellSize  = textH;
    const float cursorPos0X = ImGui::GetCursorScreenPos().x;
    const float cursorPos0Y = ImGui::GetCursorScreenPos().y;

    // Centre the glyph's VISIBLE pixels (not its em square) within the cell,
    // both horizontally and vertically.
    const float iconX = cursorPos0X + (cellSize - renderSize) * 0.5f;
    const float iconY = cursorPos0Y + (cellSize * 0.5f) - y0_scaled - glyphH_scaled * 0.5f;

    ImGui::Dummy(ImVec2(cellSize, cellSize));
    ImGui::SameLine(0.0f, 4.0f);

    ImGui::GetWindowDrawList()->AddText(
        icon.font, renderSize, ImVec2(iconX, iconY),
        ImGui::GetColorU32(ImGuiCol_Text),
        icon.glyph);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// GenericVisualizer::Draw
// ---------------------------------------------------------------------------

void GenericVisualizer::Draw(const DeviceState &dev) {
    ImGui::Text("Generic Device Visualizer");

    if (!dev.joystick)
        return;

    ImGui::Text("Name: %s", SDL_GetJoystickName(dev.joystick));

    // ── Label toggle ─────────────────────────────────────────────────────
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImGui::Checkbox("Named Inputs", &m_showLabels);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Show known input names (Left Stick X, A, D-Pad Up, …)\n"
            "with matching controller icons where available.\n"
            "Only applies to devices recognised as gamepads by SDL.");
    }

    // ── Axes ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Axes", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Size the label column from real text metrics so values are never
        // clipped — e.g. "Right Trigger: -32768" is the longest realistic
        // "name: value" pairing (axis values range -32768..32767).
        const float labelColWidth =
            ImGui::CalcTextSize("Right Trigger: -32768").x
            + ImGui::GetStyle().CellPadding.x * 2.0f;

        if (ImGui::BeginTable("AxesTable", 3,
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Icon",  ImGuiTableColumnFlags_WidthFixed,   28.0f);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,  labelColWidth);
            ImGui::TableSetupColumn("Bar",   ImGuiTableColumnFlags_WidthStretch);

            for (int i = 0; i < dev.num_axes; ++i) {
                Sint16 val  = SDL_GetJoystickAxis(dev.joystick, i);
                float  norm = static_cast<float>(val) / 32767.0f;

                ImGui::TableNextRow();

                // Icon column
                ImGui::TableSetColumnIndex(0);
                if (m_showLabels) {
                    InputLabel lbl = InputLabelProvider::GetAxisLabel(dev, i);
                    DrawInlineIcon(lbl.icon);

                    // Label column
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s: %d", lbl.name.c_str(), val);
                } else {
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("Axis %d: %d", i, val);
                }

                // Bar column
                ImGui::TableSetColumnIndex(2);
                ImGui::ProgressBar((norm + 1.0f) * 0.5f, ImVec2(-1, 0), "");

                ImVec2 p_min = ImGui::GetItemRectMin();
                ImVec2 p_max = ImGui::GetItemRectMax();
                float  cx    = (p_min.x + p_max.x) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(cx, p_min.y), ImVec2(cx, p_max.y),
                    IM_COL32(255, 255, 255, 200), 2.0f);
            }
            ImGui::EndTable();
        }
    }

    // ── Buttons ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_showLabels) {
            // Named mode: one row per button with icon + name + active highlight
            if (ImGui::BeginTable("ButtonsTable", 3,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                // "PRESSED" is the widest string this column ever shows.
                const float stateColWidth =
                    ImGui::CalcTextSize("PRESSED").x
                    + ImGui::GetStyle().CellPadding.x * 2.0f;

                ImGui::TableSetupColumn("Icon",  ImGuiTableColumnFlags_WidthFixed,  28.0f);
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed,  stateColWidth);

                for (int i = 0; i < dev.num_buttons; ++i) {
                    const bool pressed = SDL_GetJoystickButton(dev.joystick, i) != 0;
                    InputLabel lbl     = InputLabelProvider::GetButtonLabel(dev, i);

                    ImGui::TableNextRow();

                    // Highlight the whole row when pressed
                    if (pressed) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                            ImGui::GetColorU32(ImVec4(0.0f, 0.55f, 0.0f, 0.35f)));
                    }

                    ImGui::TableSetColumnIndex(0);
                    DrawInlineIcon(lbl.icon);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(
                        pressed ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f)
                                : ImGui::GetStyle().Colors[ImGuiCol_Text],
                        "%s", lbl.name.c_str());

                    ImGui::TableSetColumnIndex(2);
                    if (pressed)
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "PRESSED");
                    else
                        ImGui::TextDisabled("--");
                }
                ImGui::EndTable();
            }
        } else {
            // Numbered compact mode (original layout)
            for (int i = 0; i < dev.num_buttons; ++i) {
                if (i > 0 && i % 8 != 0)
                    ImGui::SameLine();
                bool pressed = SDL_GetJoystickButton(dev.joystick, i) != 0;
                ImGui::TextColored(
                    pressed ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f)
                            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                    "B%d", i);
            }
        }
    }

    // ── Hats ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Hats", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < dev.num_hats; ++i) {
            Uint8       hat = SDL_GetJoystickHat(dev.joystick, i);
            const char* dir = "CENTER";
            if      (hat == (SDL_HAT_RIGHT | SDL_HAT_UP))   dir = "UP-RIGHT";
            else if (hat == (SDL_HAT_RIGHT | SDL_HAT_DOWN))  dir = "DOWN-RIGHT";
            else if (hat == (SDL_HAT_LEFT  | SDL_HAT_UP))    dir = "UP-LEFT";
            else if (hat == (SDL_HAT_LEFT  | SDL_HAT_DOWN))  dir = "DOWN-LEFT";
            else if (hat & SDL_HAT_UP)    dir = "UP";
            else if (hat & SDL_HAT_DOWN)  dir = "DOWN";
            else if (hat & SDL_HAT_LEFT)  dir = "LEFT";
            else if (hat & SDL_HAT_RIGHT) dir = "RIGHT";

            if (m_showLabels) {
                InputLabel lbl = InputLabelProvider::GetHatLabel(dev, i);
                DrawInlineIcon(lbl.icon);
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::Text("%s: %s (%d)", lbl.name.c_str(), dir, hat);
            } else {
                ImGui::Text("Hat %d: %s (%d)", i, dir, hat);
            }
        }
    }
}
