#include "SteeringWheelHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/SteeringWheelHaptics.h"
#include "wheel/utils/rpm_mapper.hpp"
#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Populate the Constant edit fields from an active slot's recorded state.
void SteeringWheelHapticsVisualizer::LoadConstantFromActive(const ActiveConstantInfo& info) {
    m_constant_strength          = info.strength;
    m_constant_infinite_duration = (info.duration_ms == SDL_HAPTIC_INFINITY);
    if (!m_constant_infinite_duration)
        m_constant_duration = static_cast<int>(info.duration_ms);
}

// Populate the Periodic edit fields from an active slot's recorded state.
void SteeringWheelHapticsVisualizer::LoadPeriodicFromActive(const ActivePeriodicInfo& info) {
    m_periodic_wave_type         = static_cast<int>(info.wave_type);
    m_periodic_strength          = info.strength;
    m_periodic_period            = static_cast<int>(info.period);
    m_periodic_magnitude         = info.magnitude;
    m_periodic_offset            = info.offset;
    m_periodic_phase             = static_cast<int>(info.phase);
    m_periodic_infinite_duration = (info.duration_ms == SDL_HAPTIC_INFINITY);
    if (!m_periodic_infinite_duration)
        m_periodic_duration = static_cast<int>(info.duration_ms);
}

// Populate the Condition edit fields from an active slot's recorded state.
void SteeringWheelHapticsVisualizer::LoadConditionFromActive(const ActiveConditionInfo& info) {
    m_condition_type             = static_cast<int>(info.type);
    m_condition_right_sat        = info.right_sat;
    m_condition_left_sat         = info.left_sat;
    m_condition_right_coeff      = info.right_coeff;
    m_condition_left_coeff       = info.left_coeff;
    m_condition_deadband         = info.deadband;
    m_condition_center           = info.center;
    m_condition_infinite_duration = (info.duration_ms == SDL_HAPTIC_INFINITY);
    if (!m_condition_infinite_duration)
        m_condition_duration = static_cast<int>(info.duration_ms);
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------

void SteeringWheelHapticsVisualizer::Draw(const DeviceState& dev, DeviceManager& deviceManager) {
    HapticDevice *haptic = deviceManager.GetHapticDevice(dev.instance_id);
    if (haptic) {
        if (haptic->IsReady() && dev.joystick) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Haptics: Ready");
            SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(dev.joystick);
            if (sdl_haptic) {
                ImGui::SameLine();
                int max_effects = SDL_GetMaxHapticEffects(sdl_haptic);
                int max_playing = SDL_GetMaxHapticEffectsPlaying(sdl_haptic);

                // Count currently active slots across all effect types.
                int num_currently_playing = 0;
                auto active_constants   = haptic->GetActiveConstants();
                auto active_periodics   = haptic->GetActivePeriodicEffects();
                auto active_conditions  = haptic->GetActiveConditions();
                for (const auto& [s, i] : active_constants)  if (i.active) num_currently_playing++;
                for (const auto& [s, i] : active_periodics)  if (i.active) num_currently_playing++;
                num_currently_playing += (int)active_conditions.size();

                if (max_playing > 0 && num_currently_playing >= max_playing) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(%d/%d playing)", num_currently_playing, max_playing);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Maximum number of simultaneous effects reached!");
                    }
                } else {
                    ImGui::TextDisabled("(%d slots, %d/%d playing)", max_effects, num_currently_playing, max_playing);
                }
                SDL_CloseHaptic(sdl_haptic);
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Haptics: Not Available");
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop All Effects")) {
            haptic->StopAll();
            if (dev.gamepad) {
                SDL_RumbleGamepad(dev.gamepad, 0, 0, 0);
                SDL_RumbleGamepadTriggers(dev.gamepad, 0, 0, 0);
            }
        }
    } else {
        ImGui::TextDisabled("Haptics not available");
    }

    ImGui::Separator();
    ImGui::Text("Haptics Test");

    if (auto *wheelHaptics = dynamic_cast<SteeringWheelHaptics *>(haptic)) {

        // ----------------------------------------------------------------
        // Constant Force
        // ----------------------------------------------------------------
        if (ImGui::TreeNode("Constant Force")) {
            auto active_constants = wheelHaptics->GetActiveConstants();

            // When the user moves the slot selector, load the live values for
            // that slot (if it's running) so they're editing real state.
            int prev_const_slot = m_constant_slot;
            ImGui::SliderInt("Slot##const", &m_constant_slot, 0, 7);
            if (m_constant_slot != prev_const_slot) {
                auto it = active_constants.find(m_constant_slot);
                if (it != active_constants.end() && it->second.active)
                    LoadConstantFromActive(it->second);
            }

            bool const_changed = false;
            const_changed |= ImGui::SliderFloat("Strength", &m_constant_strength, -1.0f, 1.0f);
            const_changed |= ImGui::Checkbox("Infinite Duration##const", &m_constant_infinite_duration);
            if (!m_constant_infinite_duration)
                const_changed |= ImGui::SliderInt("Duration (ms)##const", &m_constant_duration, 0, 5000);

            // Live-update if this slot is already running and any value changed.
            bool slot_is_active = false;
            {
                auto it = active_constants.find(m_constant_slot);
                slot_is_active = (it != active_constants.end() && it->second.active);
            }
            if (const_changed && slot_is_active) {
                wheelHaptics->PlayConstant(m_constant_slot, m_constant_strength,
                                           m_constant_infinite_duration ? SDL_HAPTIC_INFINITY
                                                                        : (uint32_t)m_constant_duration);
            }

            if (slot_is_active)
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "● Slot %d active — editing live", m_constant_slot);

            if (ImGui::Button("Play Constant")) {
                wheelHaptics->PlayConstant(m_constant_slot, m_constant_strength,
                                           m_constant_infinite_duration ? SDL_HAPTIC_INFINITY
                                                                        : (uint32_t)m_constant_duration);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop Constant")) {
                wheelHaptics->StopConstant(m_constant_slot);
            }
            ImGui::TreePop();
        }

        // ----------------------------------------------------------------
        // Periodic Effects
        // ----------------------------------------------------------------
        if (ImGui::TreeNode("Periodic Effects")) {
            auto active_periodics = wheelHaptics->GetActivePeriodicEffects();

            int prev_periodic_slot = m_periodic_slot;
            ImGui::SliderInt("Slot##periodic", &m_periodic_slot, 0, 7);
            if (m_periodic_slot != prev_periodic_slot) {
                auto it = active_periodics.find(m_periodic_slot);
                if (it != active_periodics.end() && it->second.active)
                    LoadPeriodicFromActive(it->second);
            }

            const char* wave_types[] = { "Sine", "Square", "Triangle", "Sawtooth Up", "Sawtooth Down" };
            bool periodic_changed = false;
            periodic_changed |= ImGui::Combo("Wave Type##p", &m_periodic_wave_type, wave_types, IM_ARRAYSIZE(wave_types));
            periodic_changed |= ImGui::SliderFloat("Strength##p", &m_periodic_strength, 0.0f, 1.0f);
            periodic_changed |= ImGui::SliderInt("Period (ms)", &m_periodic_period, 1, 5000);
            periodic_changed |= ImGui::SliderFloat("Magnitude", &m_periodic_magnitude, 0.0f, 1.0f);
            periodic_changed |= ImGui::SliderFloat("Offset", &m_periodic_offset, -1.0f, 1.0f);
            periodic_changed |= ImGui::SliderInt("Phase", &m_periodic_phase, 0, 36000);
            periodic_changed |= ImGui::Checkbox("Infinite Duration##periodic", &m_periodic_infinite_duration);
            if (!m_periodic_infinite_duration)
                periodic_changed |= ImGui::SliderInt("Duration (ms)##periodic", &m_periodic_duration, 0, 5000);

            bool slot_is_active = false;
            {
                auto it = active_periodics.find(m_periodic_slot);
                slot_is_active = (it != active_periodics.end() && it->second.active);
            }
            if (periodic_changed && slot_is_active) {
                wheelHaptics->PlayPeriodic(m_periodic_slot,
                                           PeriodicTypeFromIndex(m_periodic_wave_type),
                                           m_periodic_strength,
                                           (uint32_t)m_periodic_period, m_periodic_magnitude,
                                           m_periodic_offset, (uint32_t)m_periodic_phase,
                                           m_periodic_infinite_duration ? SDL_HAPTIC_INFINITY
                                                                        : (uint32_t)m_periodic_duration);
            }

            if (slot_is_active)
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "● Slot %d active — editing live", m_periodic_slot);

            if (ImGui::Button("Play Periodic")) {
                wheelHaptics->PlayPeriodic(m_periodic_slot,
                                           PeriodicTypeFromIndex(m_periodic_wave_type),
                                           m_periodic_strength,
                                           (uint32_t)m_periodic_period, m_periodic_magnitude,
                                           m_periodic_offset, (uint32_t)m_periodic_phase,
                                           m_periodic_infinite_duration ? SDL_HAPTIC_INFINITY
                                                                        : (uint32_t)m_periodic_duration);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop Periodic")) {
                wheelHaptics->StopPeriodic(m_periodic_slot);
            }
            ImGui::TreePop();
        }

        // ----------------------------------------------------------------
        // Condition Effects
        // ----------------------------------------------------------------
        if (ImGui::TreeNode("Condition Effects")) {
            auto active_conditions = wheelHaptics->GetActiveConditions();

            int max_slots = 1;
            if (dev.joystick) {
                SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(dev.joystick);
                if (sdl_haptic) {
                    max_slots = SDL_GetMaxHapticEffects(sdl_haptic);
                    SDL_CloseHaptic(sdl_haptic);
                }
            }

            int prev_cond_slot = m_condition_slot;
            ImGui::SliderInt("Slot##cond", &m_condition_slot, 0, max_slots > 0 ? max_slots - 1 : 0);
            if (m_condition_slot != prev_cond_slot) {
                auto it = active_conditions.find(m_condition_slot);
                if (it != active_conditions.end())
                    LoadConditionFromActive(it->second);
            }

            const char* condition_types[] = { "Spring", "Damper", "Inertia", "Friction" };
            bool cond_changed = false;
            cond_changed |= ImGui::Combo("Type", &m_condition_type, condition_types, IM_ARRAYSIZE(condition_types));
            ImGui::Separator();
            cond_changed |= ImGui::SliderFloat("Right Sat",   &m_condition_right_sat,   0.0f,  1.0f);
            cond_changed |= ImGui::SliderFloat("Left Sat",    &m_condition_left_sat,    0.0f,  1.0f);
            cond_changed |= ImGui::SliderFloat("Right Coeff", &m_condition_right_coeff, -1.0f, 1.0f);
            cond_changed |= ImGui::SliderFloat("Left Coeff",  &m_condition_left_coeff,  -1.0f, 1.0f);
            cond_changed |= ImGui::SliderFloat("Deadband",    &m_condition_deadband,    0.0f,  1.0f);
            cond_changed |= ImGui::SliderFloat("Center",      &m_condition_center,      -1.0f, 1.0f);
            cond_changed |= ImGui::Checkbox("Infinite Duration##cond", &m_condition_infinite_duration);
            if (!m_condition_infinite_duration)
                cond_changed |= ImGui::SliderInt("Duration (ms)##cond", &m_condition_duration, 0, 10000);

            bool slot_is_active = (active_conditions.count(m_condition_slot) > 0);
            if (cond_changed && slot_is_active) {
                wheelHaptics->PlayCondition(m_condition_slot,
                                            ConditionTypeFromIndex(m_condition_type),
                                            m_condition_right_sat, m_condition_left_sat,
                                            m_condition_right_coeff, m_condition_left_coeff,
                                            m_condition_deadband, m_condition_center,
                                            m_condition_infinite_duration ? SDL_HAPTIC_INFINITY
                                                                          : (uint32_t)m_condition_duration);
            }

            if (slot_is_active)
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "● Slot %d active — editing live", m_condition_slot);

            if (ImGui::Button("Play Condition")) {
                wheelHaptics->PlayCondition(m_condition_slot,
                                            ConditionTypeFromIndex(m_condition_type),
                                            m_condition_right_sat, m_condition_left_sat,
                                            m_condition_right_coeff, m_condition_left_coeff,
                                            m_condition_deadband, m_condition_center,
                                            m_condition_infinite_duration ? SDL_HAPTIC_INFINITY
                                                                          : (uint32_t)m_condition_duration);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop Condition")) {
                wheelHaptics->StopCondition(m_condition_slot);
            }
            ImGui::TreePop();
        }

        // ----------------------------------------------------------------
        // Active Slots read-out (click a slot label to load its values)
        // ----------------------------------------------------------------
        ImGui::Separator();
        ImGui::Text("Active Haptic Slots");
        ImGui::TextDisabled("Click a slot header to load its values into the editor above.");
        if (ImGui::BeginChild("ActiveHaptics", ImVec2(0, 150), true)) {
            bool anyActive = false;

            // --- Constant ---
            auto active_constants = wheelHaptics->GetActiveConstants();
            for (const auto& [slot, info] : active_constants) {
                if (!info.active) continue;
                anyActive = true;
                bool selected = (m_constant_slot == slot);
                ImGui::PushStyleColor(ImGuiCol_Header,        selected ? ImVec4(0.2f,0.6f,0.2f,0.4f) : ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f,0.6f,0.2f,0.3f));
                bool open = ImGui::TreeNodeEx((void*)(intptr_t)(slot + 1000),
                                              ImGuiTreeNodeFlags_SpanAvailWidth,
                                              "Constant Force [slot %d]", slot);
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemClicked()) {
                    m_constant_slot = slot;
                    LoadConstantFromActive(info);
                }
                if (open) {
                    ImGui::Text("Strength: %.3f", info.strength);
                    if (info.duration_ms == SDL_HAPTIC_INFINITY)
                        ImGui::Text("Duration: Infinite");
                    else
                        ImGui::Text("Duration: %u ms", info.duration_ms);
                    ImGui::TreePop();
                }
            }

            // --- Periodic ---
            auto active_periodics = wheelHaptics->GetActivePeriodicEffects();
            for (const auto& [slot, info] : active_periodics) {
                if (!info.active) continue;
                anyActive = true;
                bool selected = (m_periodic_slot == slot);
                ImGui::PushStyleColor(ImGuiCol_Header,        selected ? ImVec4(0.2f,0.6f,0.2f,0.4f) : ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f,0.6f,0.2f,0.3f));
                bool open = ImGui::TreeNodeEx((void*)(intptr_t)(slot + 2000),
                                              ImGuiTreeNodeFlags_SpanAvailWidth,
                                              "Periodic (%s) [slot %d]", PeriodicTypeName(info.wave_type), slot);
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemClicked()) {
                    m_periodic_slot = slot;
                    LoadPeriodicFromActive(info);
                }
                if (open) {
                    ImGui::Text("Wave: %s",        PeriodicTypeName(info.wave_type));
                    ImGui::Text("Strength: %.3f",  info.strength);
                    ImGui::Text("Period: %u ms",   info.period);
                    ImGui::Text("Magnitude: %.3f", info.magnitude);
                    ImGui::Text("Offset: %.3f",    info.offset);
                    ImGui::Text("Phase: %u",       info.phase);
                    if (info.duration_ms == SDL_HAPTIC_INFINITY)
                        ImGui::Text("Duration: Infinite");
                    else
                        ImGui::Text("Duration: %u ms", info.duration_ms);
                    ImGui::TreePop();
                }
            }

            // --- Conditions ---
            auto active_conditions = wheelHaptics->GetActiveConditions();
            for (const auto& [slot, info] : active_conditions) {
                anyActive = true;
                bool selected = (m_condition_slot == slot);
                ImGui::PushStyleColor(ImGuiCol_Header,        selected ? ImVec4(0.2f,0.6f,0.2f,0.4f) : ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f,0.6f,0.2f,0.3f));
                bool open = ImGui::TreeNodeEx((void*)(intptr_t)(slot + 3000),
                                              ImGuiTreeNodeFlags_SpanAvailWidth,
                                              "Condition (%s) [slot %d]", ConditionTypeName(info.type), slot);
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemClicked()) {
                    m_condition_slot = slot;
                    LoadConditionFromActive(info);
                }
                if (open) {
                    ImGui::Text("Type: %s",                      ConditionTypeName(info.type));
                    if (info.duration_ms == SDL_HAPTIC_INFINITY)
                        ImGui::Text("Duration: Infinite");
                    else
                        ImGui::Text("Duration: %u ms",           info.duration_ms);
                    ImGui::Text("Center: %.3f",                  info.center);
                    ImGui::Text("Deadband: %.3f",                info.deadband);
                    ImGui::Text("L/R Coeff: %.3f / %.3f",        info.left_coeff,  info.right_coeff);
                    ImGui::Text("L/R Sat: %.3f / %.3f",          info.left_sat,    info.right_sat);
                    ImGui::TreePop();
                }
            }

            if (!anyActive) {
                ImGui::TextDisabled("No active haptic effects.");
                ImGui::TextDisabled("(Effects started before InputBridge opened");
                ImGui::TextDisabled(" cannot be detected via SDL.)");
            }
        }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Steering Wheel Haptics not available");
    }
}

void SteeringWheelHapticsVisualizer::DrawLEDs(DeviceManager& deviceManager) {
    ImGui::Text("RPM LEDs");

    const auto& rpmWheels = deviceManager.GetWheelRPMDevices();

    if (ImGui::Button("Scan for RPM Devices")) {
        deviceManager.ScanWheelRPMDevices();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu found)", rpmWheels.size());

    if (rpmWheels.empty()) {
        ImGui::TextDisabled("No RPM-capable wheel devices detected.");
        ImGui::TextDisabled("Connect a supported wheel and press Scan, or check");
        ImGui::TextDisabled("that the wheel is powered on.");
    } else {
        ImGui::SliderFloat("RPM %%", &m_rpm_percent, 0.0f, 1.0f, "%.2f");

        for (const auto& wheel : rpmWheels) {
            ImGui::PushID(wheel.get());

            ImGui::Text("%s", wheel->name().c_str());
            ImGui::SameLine();

            constexpr int kPreviewLEDs = 10;
            auto leds = wheel::RPMMapper::linear(m_rpm_percent, kPreviewLEDs);
            for (int i = 0; i < kPreviewLEDs; ++i) {
                ImGui::SameLine();
                ImVec4 col = leds[i]
                    ? ImVec4(1.0f, 0.4f, 0.0f, 1.0f)
                    : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                ImGui::ColorButton("##led", col,
                    ImGuiColorEditFlags_NoTooltip |
                    ImGuiColorEditFlags_NoBorder,
                    ImVec2(10, 16));
            }

            if (ImGui::Button("Set RPM")) {
                wheel->setRPM(m_rpm_percent);
            }
            ImGui::SameLine();
            if (ImGui::Button("Set LEDs")) {
                auto ledData = wheel::RPMMapper::linear(m_rpm_percent, kPreviewLEDs);
                wheel->setLEDs(ledData);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear LEDs")) {
                wheel->setRPM(0.0f);
            }

            ImGui::PopID();
        }
    }
}