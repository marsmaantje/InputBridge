#pragma once
// SidebarIcons.h
// Procedural ImDrawList icons for the sidebar navigation.
// Each function draws into a sz×sz square with top-left at `p`.
// All icons are designed to match the corresponding SVG files in icons/.

#include "imgui.h"
#include <cmath>

namespace SidebarIcons {

// ── Devices ─────────────────────────────────────────────────────────────────
// Gamepad silhouette: body, shoulder bumps, d-pad cross, face buttons.
static inline void Devices(ImDrawList* dl, ImVec2 p, float sz, ImU32 col)
{
    auto P = [&](float x, float y) -> ImVec2 { return {p.x + x*sz, p.y + y*sz}; };

    ImU32 bg = ImGui::GetColorU32(ImGuiCol_ChildBg);

    // Shoulder buttons
    dl->AddRectFilled(P(.10f,.08f), P(.44f,.32f), col, sz*.08f);
    dl->AddRectFilled(P(.56f,.08f), P(.90f,.32f), col, sz*.08f);
    // Body
    dl->AddRectFilled(P(.06f,.26f), P(.94f,.82f), col, sz*.13f);

    // D-pad (filled bg cutouts)
    float dcx = p.x+sz*.27f, dcy = p.y+sz*.56f;
    float da = sz*.13f, dt = sz*.05f;
    dl->AddRectFilled({dcx-da, dcy-dt}, {dcx+da, dcy+dt}, bg);
    dl->AddRectFilled({dcx-dt, dcy-da}, {dcx+dt, dcy+da}, bg);

    // Face buttons (two circles, bg colour)
    float bcx = p.x+sz*.73f, bcy = p.y+sz*.56f;
    dl->AddCircleFilled({bcx-sz*.10f, bcy}, sz*.065f, bg);
    dl->AddCircleFilled({bcx+sz*.10f, bcy}, sz*.065f, bg);

    // Center button (small outlined circle)
    dl->AddCircle({p.x+sz*.50f, p.y+sz*.56f}, sz*.07f, col, 10, 1.0f);
}

// ── Input Mapper ─────────────────────────────────────────────────────────────
// Three dots on the left, lines converging to a filled triangle arrow.
static inline void InputMapper(ImDrawList* dl, ImVec2 p, float sz, ImU32 col)
{
    float lx = p.x + sz*.13f;
    float cy = p.y + sz*.50f;
    float mx = p.x + sz*.50f;
    float ys[3] = {p.y+sz*.18f, p.y+sz*.50f, p.y+sz*.82f};

    for (int i = 0; i < 3; ++i) {
        dl->AddCircleFilled({lx, ys[i]}, sz*.055f, col);
        dl->AddLine({lx+sz*.06f, ys[i]}, {mx, cy}, col, 1.4f);
    }

    // Output arrow triangle
    ImVec2 a[3] = {
        {mx,           cy - sz*.23f},
        {p.x+sz*.90f,  cy          },
        {mx,           cy + sz*.23f}
    };
    dl->AddTriangleFilled(a[0], a[1], a[2], col);
}

// ── Output Mapper ────────────────────────────────────────────────────────────
// Small device body with vibration arcs on both sides.
static inline void OutputMapper(ImDrawList* dl, ImVec2 p, float sz, ImU32 col)
{
    float cx = p.x + sz*.50f, cy = p.y + sz*.52f;
    float bw = sz*.26f, bh = sz*.22f;
    float r  = sz*.07f;

    // Device body (filled)
    dl->AddRectFilled({cx-bw, cy-bh}, {cx+bw, cy+bh}, col, r);

    // Vibration arcs - make the far arc slightly transparent
    ImU32 dim = (col & 0x00FFFFFFu) |
                (static_cast<ImU32>(static_cast<float>((col >> 24) & 0xFF) * 0.55f) << 24);

    // Left arcs
    float la = cx - bw;
    float arc1 = sz*.22f, arc2 = sz*.36f;
    float aS = IM_PI * 0.60f, aE = IM_PI * 1.40f;  // left-facing half
    dl->PathArcTo({la, cy}, arc1, aS, aE, 10); dl->PathStroke(col, false, 1.5f);
    dl->PathArcTo({la, cy}, arc2, aS, aE, 10); dl->PathStroke(dim, false, 1.5f);

    // Right arcs
    float ra = cx + bw;
    float bS = -IM_PI * 0.40f, bE = IM_PI * 0.40f; // right-facing half
    dl->PathArcTo({ra, cy}, arc1, bS, bE, 10); dl->PathStroke(col, false, 1.5f);
    dl->PathArcTo({ra, cy}, arc2, bS, bE, 10); dl->PathStroke(dim, false, 1.5f);
}

// ── Network ──────────────────────────────────────────────────────────────────
// Wifi symbol: filled dot at bottom + three ascending arcs.
static inline void Network(ImDrawList* dl, ImVec2 p, float sz, ImU32 col)
{
    float cx    = p.x + sz * .50f;
    float dot_y = p.y + sz * .86f;

    dl->AddCircleFilled({cx, dot_y}, sz*.065f, col);

    float radii[3]   = {sz*.18f, sz*.34f, sz*.50f};
    float alphaF[3]  = {1.0f,    1.0f,    0.60f};
    float sweep      = IM_PI * .42f;
    float top        = -IM_PI * .50f;            // -90° = pointing up

    for (int i = 0; i < 3; ++i) {
        ImU32 c = (col & 0x00FFFFFFu) |
                  (static_cast<ImU32>(static_cast<float>((col >> 24) & 0xFF) * alphaF[i]) << 24);
        dl->PathArcTo({cx, dot_y}, radii[i], top - sweep, top + sweep, 14);
        dl->PathStroke(c, false, 1.5f);
    }
}

// ── Protocol Editor ──────────────────────────────────────────────────────────
// Document outline with folded top-right corner + horizontal code lines.
static inline void ProtocolEditor(ImDrawList* dl, ImVec2 p, float sz, ImU32 col)
{
    float x0 = p.x+sz*.16f, y0 = p.y+sz*.04f;
    float x1 = p.x+sz*.84f, y1 = p.y+sz*.96f;
    float fold = sz*.22f;

    // Outline with fold
    dl->PathLineTo({x0,         y0         });
    dl->PathLineTo({x1 - fold,  y0         });
    dl->PathLineTo({x1,         y0 + fold  });
    dl->PathLineTo({x1,         y1         });
    dl->PathLineTo({x0,         y1         });
    dl->PathStroke(col, true, 1.5f);

    // Fold crease
    dl->AddLine({x1-fold, y0},       {x1-fold, y0+fold}, col, 1.0f);
    dl->AddLine({x1-fold, y0+fold},  {x1,      y0+fold}, col, 1.0f);

    // Text lines
    float lx0 = x0+sz*.09f;
    float lx1 = x1-sz*.06f;
    float lys[3] = {y0+sz*.36f, y0+sz*.54f, y0+sz*.72f};
    dl->AddLine({lx0, lys[0]}, {lx0+(lx1-lx0)*.75f, lys[0]}, col, 1.2f);
    dl->AddLine({lx0, lys[1]}, {lx1,                 lys[1]}, col, 1.2f);
    dl->AddLine({lx0, lys[2]}, {lx0+(lx1-lx0)*.55f, lys[2]}, col, 1.2f);
}

// ── UI Settings ──────────────────────────────────────────────────────────────
// 8-tooth gear: star polygon fill + center hole.
static inline void UISettings(ImDrawList* dl, ImVec2 p, float sz, ImU32 col)
{
    float cx     = p.x + sz*.50f;
    float cy     = p.y + sz*.50f;
    float outer  = sz*.42f;
    float inner  = sz*.29f;
    float hole   = sz*.14f;
    int   n      = 8;
    float offset = -IM_PI * .50f;  // start at top

    dl->PathClear();
    for (int i = 0; i < n * 2; ++i) {
        float angle = offset + static_cast<float>(i) / static_cast<float>(n * 2) * IM_PI * 2.0f;
        float r     = (i & 1) ? inner : outer;
        dl->PathLineTo({cx + cosf(angle)*r, cy + sinf(angle)*r});
    }
    dl->PathFillConvex(col);

    // Center hole (overdraw with background colour)
    ImU32 bg = ImGui::GetColorU32(ImGuiCol_ChildBg);
    dl->AddCircleFilled({cx, cy}, hole, bg);
    dl->AddCircle      ({cx, cy}, hole, col, 16, 1.2f);
}

// ── Exit ─────────────────────────────────────────────────────────────────────
// Power-button symbol: circle arc with gap at top + vertical stem.
static inline void Exit(ImDrawList* dl, ImVec2 p, float sz, ImU32 col)
{
    float cx  = p.x + sz*.50f;
    float cy  = p.y + sz*.54f;
    float r   = sz*.33f;
    float gap = IM_PI * .22f;  // angular gap at top

    float top = -IM_PI * .50f;
    dl->PathArcTo({cx, cy}, r, top + gap, top + IM_PI*2.0f - gap, 22);
    dl->PathStroke(col, false, 2.0f);

    // Stem
    dl->AddLine({cx, cy - r + sz*.04f}, {cx, cy - sz*.07f}, col, 2.0f);
}

} // namespace SidebarIcons
