/**
 * @file EditableSlider.h
 * @brief Drop-in ImGui::SliderInt/SliderFloat wrappers with an optional
 *        "pen" button for typing an exact value.
 *
 * ImGui's sliders already support Ctrl+Click to drop into a text-entry box
 * (imgui_widgets.cpp: SliderScalar() only enters TempInputScalar() when it
 * sees a genuine same-frame IsMouseClicked() on the slider's own id with
 * io.KeyCtrl held, or a nav-activate with ImGuiActivateFlags_PreferInput -
 * both internal-state checks with no public "force this" entry point). A
 * button elsewhere on screen can't synthesize that click on the slider, so
 * rather than fight ImGui's internals to fake Ctrl+Click from a button, the
 * pen button here swaps the row to a real ImGui::InputInt/InputFloat box
 * (both fully public API) and swaps back once the user confirms (Enter) or
 * clicks away (IsItemDeactivated). This needs no imgui_internal.h include
 * and doesn't depend on ImGui version-specific internal field names.
 *
 * Per-widget edit-mode state is tracked via ImGui's own GetStateStorage(),
 * keyed by the slider's id - the same id ImGui's own ID stack already
 * disambiguates for duplicate labels (many call sites in this codebase
 * already rely on that via PushID for e.g. repeated "Strength" sliders).
 *
 * Visibility of the pen button is controlled by a single process-wide flag
 * (SetSliderEditButtonsEnabled), set once from Application based on the
 * "Slider Edit Buttons" Settings checkbox, rather than threading a bool
 * through every call site - there are ~90 sliders across half a dozen
 * visualizer files, all in the same executable/UI thread, so a small owned
 * flag here is simpler than plumbing a new parameter through every
 * intervening Draw() signature for a display-only preference.
 *
 * Usage mirrors the native calls:
 * @code
 * UI::SliderInt("Left Intensity", &m_left, 0, 255);
 * UI::SliderFloat("Strength", &m_strength, -1.0f, 1.0f);
 * @endcode
 */
#pragma once

namespace UI {

/// Sets whether the pen edit button is drawn next to sliders app-wide.
/// Called once from Application::RestorePreferences() and again whenever
/// the "Slider Edit Buttons" Settings checkbox changes.
void SetSliderEditButtonsEnabled(bool enabled);

/// Returns the current value set by SetSliderEditButtonsEnabled(), default true.
bool GetSliderEditButtonsEnabled();

/// Drop-in replacement for ImGui::SliderInt. When edit buttons are enabled,
/// draws a pen button after the slider that swaps it for a text-entry box
/// (confirm with Enter or click away to return to the slider). Note that
/// Ctrl+Click on the slider itself still works too, as with any ImGui
/// slider - this button is an additional, more discoverable affordance for
/// the same need, not a replacement for it.
///
/// Because the pen button is drawn as part of this call, ImGui::IsItemHovered()
/// checked by the caller immediately afterwards would refer to the pen
/// button rather than the slider/input box. Pass a non-null tooltip here
/// instead of doing that check externally - it's shown when the
/// slider/input box itself (not the pen button) is hovered, matching what
/// ImGui::IsItemHovered() + ImGui::SetTooltip() right after a plain
/// ImGui::SliderInt() call would have done.
/// @return true if the value changed this frame (slider drag or typed entry).
bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d", const char* tooltip = nullptr);

/// Drop-in replacement for ImGui::SliderFloat. See SliderInt() for behavior.
bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", const char* tooltip = nullptr);

} // namespace UI
