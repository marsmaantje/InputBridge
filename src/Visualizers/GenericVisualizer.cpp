#include "GenericVisualizer.h"
#include "UI/InputLabelProvider.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <SDL3/SDL.h>

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

    // Measure the visible pixel height of this glyph so we can scale it to
    // match the text line height precisely.
    const float bakeSize   = ImGui::GetStyle().FontSizeBase * 4.0f;
    float       renderSize = textH; // fallback: render at 1:1 em

    if (ImFontBaked* baked = icon.font->GetFontBaked(bakeSize))
    {
        const ImFontGlyph* g = baked->FindGlyphNoFallback(icon.codepoint);
        if (g && baked->Size > 0.0f)
        {
            const float glyphH = g->Y1 - g->Y0;
            const float fill   = glyphH / baked->Size;
            if (fill > 0.01f)
                renderSize = textH / fill;
        }
    }

    // Vertically centre the em-square so the visible pixels sit on the
    // text baseline band.
    const float cursorY   = ImGui::GetCursorScreenPos().y;
    const float offsetY   = (textH - renderSize) * 0.5f;
    ImVec2      iconPos   = { ImGui::GetCursorScreenPos().x,
                               cursorY + offsetY };

    // Reserve a square cursor-advance equal to the render size.
    ImGui::Dummy(ImVec2(renderSize, textH));
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::GetWindowDrawList()->AddText(
        icon.font, renderSize, iconPos,
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
        if (ImGui::BeginTable("AxesTable", 3,
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Icon",  ImGuiTableColumnFlags_WidthFixed,   28.0f);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,  150.0f);
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
                ImGui::TableSetupColumn("Icon",  ImGuiTableColumnFlags_WidthFixed,  28.0f);
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed,  50.0f);

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
