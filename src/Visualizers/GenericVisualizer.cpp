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
    // match the text line - but never beyond it, or the icon cell ends up
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
        icon.glyph());
}

// Draws the progress bar + center tick for one axis row in whichever is the
// current (last) table column. Shared by both the labeled and unlabeled
// Axes table layouts below.
void DrawAxisBar(float norm)
{
    ImGui::ProgressBar((norm + 1.0f) * 0.5f, ImVec2(-1, 0), "");

    ImVec2 p_min = ImGui::GetItemRectMin();
    ImVec2 p_max = ImGui::GetItemRectMax();
    float  cx    = (p_min.x + p_max.x) * 0.5f;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(cx, p_min.y), ImVec2(cx, p_max.y),
        IM_COL32(255, 255, 255, 200), 2.0f);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// GenericVisualizer::Draw
// ---------------------------------------------------------------------------

void GenericVisualizer::Draw(const DeviceState &dev, bool m_showLabels) {
    ImGui::Text("Generic Device Visualizer");

    if (!dev.joystick)
        return;

    ImGui::Text("Name: %s", SDL_GetJoystickName(dev.joystick));

    // -- Axes -------------------------------------------------------------
    if (ImGui::CollapsingHeader("Axes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_showLabels) {
            // Size the label column from real text metrics so values are never
            // clipped - e.g. "Right Trigger: -32768" is the longest realistic
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

                    ImGui::TableSetColumnIndex(0);
                    InputLabel lbl = InputLabelProvider::GetAxisLabel(dev, i);
                    DrawInlineIcon(lbl.icon);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s: %d", lbl.name.c_str(), val);

                    ImGui::TableSetColumnIndex(2);
                    DrawAxisBar(norm);
                }
                ImGui::EndTable();
            }
        } else {
            // Numbered compact mode: no icon column at all (there is no icon
            // to show), so the table is genuinely 2 columns rather than a
            // 3-column table with an unused, empty icon cell.
            const float labelColWidth =
                ImGui::CalcTextSize("Axis 99: -32768").x
                + ImGui::GetStyle().CellPadding.x * 2.0f;

            if (ImGui::BeginTable("AxesTable", 2,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
                ImGui::TableSetupColumn("Bar",   ImGuiTableColumnFlags_WidthStretch);

                for (int i = 0; i < dev.num_axes; ++i) {
                    Sint16 val  = SDL_GetJoystickAxis(dev.joystick, i);
                    float  norm = static_cast<float>(val) / 32767.0f;

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Axis %d: %d", i, val);

                    ImGui::TableSetColumnIndex(1);
                    DrawAxisBar(norm);
                }
                ImGui::EndTable();
            }
        }
    }

    // -- Buttons ----------------------------------------------------------
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_showLabels) {
            // Named mode: icons flow inline just like the numbered compact
            // layout, but each slot renders the Kenney glyph (or the button
            // name as a fallback) and tints green when pressed.
            const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
            const float availW      = ImGui::GetContentRegionAvail().x;
            const float cellSize    = ImGui::GetTextLineHeight(); // minimum slot width

            for (int i = 0; i < dev.num_buttons; ++i) {
                const bool pressed = SDL_GetJoystickButton(dev.joystick, i) != 0;
                InputLabel lbl     = InputLabelProvider::GetButtonLabel(dev, i);

                // -- Per-glyph size probe ----------------------------------
                // We must know the slot width *before* the wrap/SameLine
                // decision, so probe the glyph metrics up front and reuse
                // the results when drawing.
                const float textH    = ImGui::GetTextLineHeight();
                const float bakeSize = ImGui::GetStyle().FontSizeBase * 4.0f;
                float renderSize     = textH;
                float y0_scaled      = 0.0f;
                float glyphH_scaled  = textH;

                if (lbl.icon.IsValid()) {
                    if (ImFontBaked* baked = lbl.icon.font->GetFontBaked(bakeSize)) {
                        const ImFontGlyph* g = baked->FindGlyphNoFallback(lbl.icon.codepoint);
                        if (g && baked->Size > 0.0f) {
                            const float glyphH = g->Y1 - g->Y0;
                            const float fill   = glyphH / baked->Size;
                            if (fill > 0.01f) {
                                renderSize    = std::min(textH / fill, textH * 2.5f);
                                y0_scaled     = (g->Y0 / baked->Size) * renderSize;
                                glyphH_scaled = (glyphH / baked->Size) * renderSize;
                            }
                        }
                    }
                }

                // The slot must be at least as wide as the glyph we will draw.
                // Using a fixed cellSize (= textH) was the bug: glyphs with a
                // low fill ratio get a large renderSize and bleed into the next
                // slot when only textH of Dummy space was reserved.
                const float slotSizeActual = std::max(renderSize, cellSize);
                const float slotW          = slotSizeActual + itemSpacing;

                // -- Wrap / SameLine ---------------------------------------
                if (i > 0) {
                    float curX = ImGui::GetCursorScreenPos().x
                                 - ImGui::GetWindowPos().x
                                 - ImGui::GetScrollX();
                    if (curX + slotW <= availW)
                        ImGui::SameLine(0.0f, itemSpacing);
                }

                // Choose tint: bright green when pressed, dim when released.
                const ImVec4 tint = pressed
                    ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                    : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

                // -- Draw -------------------------------------------------
                if (lbl.icon.IsValid()) {
                    const ImVec2 cursor = ImGui::GetCursorScreenPos();
                    const float  iconX  = cursor.x + (slotSizeActual - renderSize) * 0.5f;
                    // Height is always cellSize - only the width varies per glyph.
                    // Centering vertically within cellSize, same as DrawInlineIcon.
                    const float  iconY  = cursor.y + (cellSize * 0.5f)
                                          - y0_scaled - glyphH_scaled * 0.5f;

                    // Width = slotSizeActual so wide glyphs don't bleed into
                    // the next slot. Height = cellSize so rows stay flush.
                    ImGui::Dummy(ImVec2(slotSizeActual, cellSize));
                    ImGui::GetWindowDrawList()->AddText(
                        lbl.icon.font, renderSize, ImVec2(iconX, iconY),
                        ImGui::GetColorU32(tint),
                        lbl.icon.glyph());
                } else {
                    // No icon - fall back to the short button name.
                    ImGui::TextColored(tint, "%s", lbl.name.c_str());
                }
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

    // -- Hats -------------------------------------------------------------
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
                InputLabel lbl = InputLabelProvider::GetHatLabel(dev, i, hat);

                // GetHatLabel() already resolves to a single icon that
                // matches the held direction - for the Wii font that's one
                // of the distinct per-direction held/outline glyph pairs
                // (KENNEY_WII_DPAD_{UP,DOWN,LEFT,RIGHT}_CP, each visually
                // distinct from the others, with KENNEY_WII_DPAD_NONE_CP
                // for the centered/idle state), so just draw it directly.
                DrawInlineIcon(lbl.icon);
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::Text("%s: %s (%d)", lbl.name.c_str(), dir, hat);
            } else {
                ImGui::Text("Hat %d: %s (%d)", i, dir, hat);
            }
        }
    }
}
