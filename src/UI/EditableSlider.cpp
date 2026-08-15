#include "EditableSlider.h"
#include "UI/IconsFontAwesome6.h"
#include "imgui.h"

namespace UI {

namespace {
    bool s_editButtonsEnabled = true;

    // Per-widget "currently showing the typed-entry box instead of the
    // slider" state, keyed by the slider's own ImGui id via GetStateStorage.
    // This piggybacks on ImGui's existing ID stack, so it composes
    // correctly with call sites that already PushID() to disambiguate
    // duplicate labels (e.g. the DualSense trigger param sliders).
    bool IsEditing(ImGuiID id) {
        return ImGui::GetStateStorage()->GetBool(id, false);
    }
    void SetEditing(ImGuiID id, bool editing) {
        ImGui::GetStateStorage()->SetBool(id, editing);
    }

    // Draws the pen button immediately after whatever was just drawn (same
    // line). Toggles edit mode on press. No-op when edit buttons are
    // disabled in Settings, so disabling the option removes the button
    // entirely rather than just hiding it behind another click.
    void DrawPenButton(ImGuiID id, bool currentlyEditing) {
        if (!s_editButtonsEnabled) return;

        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(id));
        const char* icon = currentlyEditing ? ICON_FA_CHECK : ICON_FA_PEN;
        if (ImGui::SmallButton(icon)) {
            SetEditing(id, !currentlyEditing);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(currentlyEditing ? "Done editing" : "Type an exact value");
        ImGui::PopID();
    }
}

void SetSliderEditButtonsEnabled(bool enabled) {
    s_editButtonsEnabled = enabled;
    // Note: doesn't force any in-progress edit boxes back to slider mode -
    // they'll revert next time the user confirms (Enter) or clicks away,
    // same as if the setting had been left enabled. Not worth the extra
    // state tracking for what's a rare mid-edit toggle-off.
}

bool GetSliderEditButtonsEnabled() {
    return s_editButtonsEnabled;
}

bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format, const char* tooltip) {
    const ImGuiID id = ImGui::GetID(label);
    const bool editing = s_editButtonsEnabled && IsEditing(id);

    bool changed = false;
    if (editing) {
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
        changed = ImGui::InputInt(label, v, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue);
        if (*v < v_min) *v = v_min;
        if (*v > v_max) *v = v_max;
        // Confirmed with Enter, or clicked/tabbed away - either way, drop
        // back to the slider. IsItemDeactivated() covers "clicked away
        // without changing anything"; the EnterReturnsTrue branch covers
        // "confirmed with a new value".
        if (changed || ImGui::IsItemDeactivated())
            SetEditing(id, false);
    } else {
        changed = ImGui::SliderInt(label, v, v_min, v_max, format);
    }

    // Checked immediately after the slider/input widget (before the pen
    // button below), so this refers to that widget and not the pen button.
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);

    DrawPenButton(id, editing);
    return changed;
}

bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, const char* tooltip) {
    const ImGuiID id = ImGui::GetID(label);
    const bool editing = s_editButtonsEnabled && IsEditing(id);

    bool changed = false;
    if (editing) {
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
        changed = ImGui::InputFloat(label, v, 0.0f, 0.0f, format, ImGuiInputTextFlags_EnterReturnsTrue);
        if (*v < v_min) *v = v_min;
        if (*v > v_max) *v = v_max;
        if (changed || ImGui::IsItemDeactivated())
            SetEditing(id, false);
    } else {
        changed = ImGui::SliderFloat(label, v, v_min, v_max, format);
    }

    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);

    DrawPenButton(id, editing);
    return changed;
}

} // namespace UI
