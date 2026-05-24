#include "VirtualDeviceVisualizer.h"
#include "imgui.h"
#include <SDL3/SDL.h>

// Hat direction names and SDL_HAT_* constant pairs.
static const struct { const char* label; Uint8 mask; } kHatDirs[] = {
    { "UL", SDL_HAT_LEFTUP   }, { "U",  SDL_HAT_UP    }, { "UR", SDL_HAT_RIGHTUP   },
    { "L",  SDL_HAT_LEFT     }, { " \xC2\xB7", SDL_HAT_CENTERED }, { "R",  SDL_HAT_RIGHT    },
    { "DL", SDL_HAT_LEFTDOWN }, { "D",  SDL_HAT_DOWN  }, { "DR", SDL_HAT_RIGHTDOWN },
};

// ─────────────────────────────────────────────────────────────────────────────
// Hat widget — a 3x3 grid of buttons for 8 directions + centred.
// Returns true if the hat value changed.
// ─────────────────────────────────────────────────────────────────────────────
static bool DrawHatWidget(Uint8& hat) {
    bool changed = false;
    const float  btnSz  = ImGui::GetFontSize() * 2.0f;
    const ImVec4 active = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    const ImVec4 normal = ImGui::GetStyleColorVec4(ImGuiCol_Button);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (col > 0) ImGui::SameLine();
            const auto& d     = kHatDirs[row * 3 + col];
            bool        on    = (hat == d.mask);

            // Center button clears to CENTERED
            ImGui::PushID(row * 3 + col);
            ImGui::PushStyleColor(ImGuiCol_Button,
                on ? active : normal);

            if (ImGui::Button(d.label, ImVec2(btnSz, btnSz))) {
                hat     = on ? SDL_HAT_CENTERED : d.mask;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main draw function
// ─────────────────────────────────────────────────────────────────────────────
void VirtualDeviceVisualizer::Draw(const DeviceState& dev) {
    auto& mgr   = VirtualDeviceManager::GetInstance();
    auto* state = mgr.GetState(dev.instance_id);

    if (!state) {
        ImGui::TextDisabled("No virtual device state found for this device.");
        return;
    }

    bool dirty = false;

    // ── Axes ─────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Axes", ImGuiTreeNodeFlags_DefaultOpen)) {

        const float labelColW = ImGui::CalcTextSize("R Trigger").x + 8.0f;
        const float sliderW   = ImGui::GetContentRegionAvail().x
                                - labelColW
                                - ImGui::GetStyle().ItemSpacing.x * 2.0f
                                - 70.0f; // space for numeric readout

        ImGui::PushID("axes");
        for (int i = 0; i < static_cast<int>(state->axes.size()); ++i) {
            ImGui::PushID(i);

            // Fixed-width label column
            ImGui::Text("%-10s", state->axisInfo[i].label);
            ImGui::SameLine(labelColW);

            // Slider
            ImGui::SetNextItemWidth(sliderW > 50.0f ? sliderW : 50.0f);
            if (ImGui::SliderFloat("##ax", &state->axes[i], -1.0f, 1.0f, "%.3f"))
                dirty = true;

            // Reset button
            ImGui::SameLine();
            if (ImGui::SmallButton("R")) {
                state->axes[i] = state->axisInfo[i].defaultValue;
                dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset to default");

            ImGui::PopID();
        }
        ImGui::PopID(); // "axes"
    }

    // ── Buttons ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {

        const float btnSz   = ImGui::GetFontSize() * 2.8f;
        const int   perRow  = static_cast<int>(
            (ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x)
            / (btnSz + ImGui::GetStyle().ItemSpacing.x));

        const ImVec4 colorOn  = ImVec4(0.15f, 0.70f, 0.25f, 1.0f);
        const ImVec4 colorOff = ImGui::GetStyleColorVec4(ImGuiCol_Button);

        ImGui::PushID("buttons");
        for (int i = 0; i < static_cast<int>(state->buttons.size()); ++i) {
            if (i > 0 && i % perRow != 0) ImGui::SameLine();
            ImGui::PushID(i);

            bool pressed = state->buttons[i];
            ImGui::PushStyleColor(ImGuiCol_Button, pressed ? colorOn : colorOff);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                pressed ? ImVec4(0.20f, 0.80f, 0.30f, 1.0f)
                        : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));

            if (ImGui::Button(std::to_string(i).c_str(), ImVec2(btnSz, btnSz))) {
                state->buttons[i] = !state->buttons[i];
                dirty   = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Button %d\nClick to toggle", i);

            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        ImGui::PopID(); // "buttons"

        // Quick-clear row
        ImGui::Spacing();
        if (ImGui::SmallButton("Release All Buttons")) {
            std::fill(state->buttons.begin(), state->buttons.end(), false);
            dirty = true;
        }
    }

    // ── Hat ──────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Hat / D-Pad", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Hat direction:");
        ImGui::SameLine();

        // Find current name
        const char* cur = "Center";
        for (auto& d : kHatDirs)
            if (d.mask == state->hat) { cur = d.label; break; }
        ImGui::TextDisabled("(%s)", cur);

        ImGui::PushID("hat");
        if (DrawHatWidget(state->hat))
            dirty = true;
        ImGui::PopID(); // "hat"
    }

    // ── Reset all ────────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reset All Inputs")) {
        for (int i = 0; i < static_cast<int>(state->axes.size()); ++i)
            state->axes[i] = state->axisInfo[i].defaultValue;
        std::fill(state->buttons.begin(), state->buttons.end(), false);
        state->hat = SDL_HAT_CENTERED;
        dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset all axes to centre, release all buttons");

    // ── Push to SDL ──────────────────────────────────────────────────────────
    // Always push every frame so that the InputMapper sees current values even
    // if only the SDL event loop has changed (e.g. on first display).
    mgr.PushState(dev.instance_id);
    (void)dirty; // used conceptually; PushState is cheap, so we always call it
}