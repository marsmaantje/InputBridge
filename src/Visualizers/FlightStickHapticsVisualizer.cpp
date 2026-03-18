#include "FlightStickHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/FlightStickHaptics.h"
#include <SDL3/SDL.h>

void FlightStickHapticsVisualizer::Draw(const DeviceState& dev, DeviceManager& deviceManager) {
    HapticDevice* haptic = deviceManager.GetHapticDevice(dev.instance_id);

    // --- Status bar ---
    if (haptic) {
        if (haptic->IsReady() && dev.joystick) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Haptics: Ready");

            SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(dev.joystick);
            if (sdl_haptic) {
                ImGui::SameLine();
                int max_effects = SDL_GetMaxHapticEffects(sdl_haptic);
                int max_playing = SDL_GetMaxHapticEffectsPlaying(sdl_haptic);

                int num_playing = 0;
                auto active_constants  = haptic->GetActiveConstants();
                auto active_periodics  = haptic->GetActivePeriodicEffects();
                auto active_conditions = haptic->GetActiveConditions();
                auto active_rumbles    = haptic->GetActiveRumbles();
                for (const auto& [s, i] : active_constants)  if (i.active) num_playing++;
                for (const auto& [s, i] : active_periodics)  if (i.active) num_playing++;
                for (const auto& [s, i] : active_rumbles)    if (i.active) num_playing++;
                num_playing += static_cast<int>(active_conditions.size());

                if (max_playing > 0 && num_playing >= max_playing) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                                       "(%d/%d playing)", num_playing, max_playing);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Maximum simultaneous effects reached!");
                } else {
                    ImGui::TextDisabled("(%d slots, %d/%d playing)",
                                        max_effects, num_playing, max_playing);
                }
                SDL_CloseHaptic(sdl_haptic);
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Haptics: Not Available");
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop All Effects"))
            haptic->StopAll();
    } else {
        ImGui::TextDisabled("Haptics not available for this device.");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Haptics Test");

    auto* fsHaptics = dynamic_cast<FlightStickHaptics*>(haptic);
    if (!fsHaptics) {
        ImGui::TextDisabled("Flight Stick Haptics not available.");
        return;
    }

    // -----------------------------------------------------------------------
    // Constant Force
    // -----------------------------------------------------------------------
    if (ImGui::TreeNode("Constant Force")) {
        ImGui::TextDisabled("Simulates sustained aerodynamic load or G-force on the stick.");
        ImGui::SliderInt("Slot##const_fs", &m_constant_slot, 0, 7);
        ImGui::SliderFloat("Strength##const_fs", &m_constant_strength, -1.0f, 1.0f);
        ImGui::Checkbox("Infinite Duration##const_fs", &m_constant_infinite_duration);
        if (!m_constant_infinite_duration)
            ImGui::SliderInt("Duration (ms)##const_fs", &m_constant_duration, 0, 5000);

        if (ImGui::Button("Play Constant##fs")) {
            fsHaptics->PlayConstant(
                m_constant_slot, m_constant_strength,
                m_constant_infinite_duration ? SDL_HAPTIC_INFINITY
                                             : static_cast<uint32_t>(m_constant_duration));
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Constant##fs"))
            fsHaptics->StopConstant(m_constant_slot);

        ImGui::TreePop();
    }

    // -----------------------------------------------------------------------
    // Periodic
    // -----------------------------------------------------------------------
    if (ImGui::TreeNode("Periodic Effects")) {
        ImGui::TextDisabled("Engine hum, turbulence, weapons fire, buffet effects.");
        const char* wave_types[] = { "Sine", "Square", "Triangle", "Sawtooth Up", "Sawtooth Down" };
        ImGui::Combo("Wave Type##per_fs", &m_periodic_wave_type, wave_types, IM_ARRAYSIZE(wave_types));
        ImGui::SliderInt("Slot##per_fs", &m_periodic_slot, 0, 7);
        ImGui::SliderFloat("Strength##per_fs",   &m_periodic_strength,  0.0f, 1.0f);
        ImGui::SliderInt("Period (ms)##per_fs",  &m_periodic_period,    1, 2000);
        ImGui::SliderFloat("Magnitude##per_fs",  &m_periodic_magnitude, 0.0f, 1.0f);
        ImGui::SliderFloat("Offset##per_fs",     &m_periodic_offset,   -1.0f, 1.0f);
        ImGui::SliderInt("Phase (0.01°)##per_fs",&m_periodic_phase,     0, 36000);
        ImGui::Checkbox("Infinite Duration##per_fs", &m_periodic_infinite_duration);
        if (!m_periodic_infinite_duration)
            ImGui::SliderInt("Duration (ms)##per_fs", &m_periodic_duration, 0, 5000);

        if (ImGui::Button("Play Periodic##fs")) {
            fsHaptics->PlayPeriodic(
                m_periodic_slot, PeriodicTypeFromIndex(m_periodic_wave_type),
                m_periodic_strength,
                static_cast<uint32_t>(m_periodic_period), m_periodic_magnitude,
                m_periodic_offset, static_cast<uint32_t>(m_periodic_phase),
                m_periodic_infinite_duration ? SDL_HAPTIC_INFINITY
                                             : static_cast<uint32_t>(m_periodic_duration));
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Periodic##fs"))
            fsHaptics->StopPeriodic(m_periodic_slot);

        ImGui::TreePop();
    }

    // -----------------------------------------------------------------------
    // Condition Effects
    // -----------------------------------------------------------------------
    if (ImGui::TreeNode("Condition Effects")) {
        ImGui::TextDisabled("Position-dependent forces: centering (Spring), drag (Damper),\n"
                            "mass (Inertia), or roughness (Friction). Applied on both axes.");

        const char* condition_types[] = { "Spring", "Damper", "Inertia", "Friction" };
        ImGui::Combo("Type##cond_fs", &m_condition_type, condition_types,
                     IM_ARRAYSIZE(condition_types));
        ImGui::Separator();

        int max_slots = 1;
        if (dev.joystick) {
            SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(dev.joystick);
            if (sdl_haptic) {
                max_slots = SDL_GetMaxHapticEffects(sdl_haptic);
                SDL_CloseHaptic(sdl_haptic);
            }
        }
        ImGui::SliderInt("Slot##cond_fs", &m_condition_slot, 0,
                         max_slots > 0 ? max_slots - 1 : 0);

        ImGui::SliderFloat("Right Sat##cond_fs",   &m_condition_right_sat,    0.0f,  1.0f);
        ImGui::SliderFloat("Left Sat##cond_fs",    &m_condition_left_sat,     0.0f,  1.0f);
        ImGui::SliderFloat("Right Coeff##cond_fs", &m_condition_right_coeff, -1.0f,  1.0f);
        ImGui::SliderFloat("Left Coeff##cond_fs",  &m_condition_left_coeff,  -1.0f,  1.0f);
        ImGui::SliderFloat("Deadband##cond_fs",    &m_condition_deadband,     0.0f,  1.0f);
        ImGui::SliderFloat("Center##cond_fs",      &m_condition_center,      -1.0f,  1.0f);
        ImGui::Checkbox("Infinite Duration##cond_fs", &m_condition_infinite_duration);
        if (!m_condition_infinite_duration)
            ImGui::SliderInt("Duration (ms)##cond_fs", &m_condition_duration, 0, 10000);

        if (ImGui::Button("Play Condition##fs")) {
            fsHaptics->PlayCondition(
                m_condition_slot, ConditionTypeFromIndex(m_condition_type),
                m_condition_right_sat, m_condition_left_sat,
                m_condition_right_coeff, m_condition_left_coeff,
                m_condition_deadband, m_condition_center,
                m_condition_infinite_duration ? SDL_HAPTIC_INFINITY
                                              : static_cast<uint32_t>(m_condition_duration));
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Condition##fs"))
            fsHaptics->StopCondition(m_condition_slot);

        ImGui::TreePop();
    }

    // -----------------------------------------------------------------------
    // Rumble (simulated via periodic)
    // -----------------------------------------------------------------------
    if (ImGui::TreeNode("Rumble (Impact)")) {
        ImGui::TextDisabled("Simulates impact hits via a low-frequency vibration burst.\n"
                            "Implemented as a periodic effect on haptic-only devices.");
        ImGui::SliderInt("Slot##rum_fs", &m_rumble_slot, 0, 7);
        ImGui::SliderFloat("Large Motor##rum_fs", &m_rumble_large, 0.0f, 1.0f);
        ImGui::SliderFloat("Small Motor##rum_fs", &m_rumble_small, 0.0f, 1.0f);
        ImGui::Checkbox("Infinite Duration##rum_fs", &m_rumble_infinite_duration);
        if (!m_rumble_infinite_duration)
            ImGui::SliderInt("Duration (ms)##rum_fs", &m_rumble_duration, 0, 2000);

        if (ImGui::Button("Play Rumble##fs")) {
            fsHaptics->PlayRumble(
                m_rumble_slot, m_rumble_large, m_rumble_small,
                m_rumble_infinite_duration ? SDL_HAPTIC_INFINITY
                                           : static_cast<uint32_t>(m_rumble_duration));
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Rumble##fs"))
            fsHaptics->StopPeriodic(m_rumble_slot);  // Rumble reuses periodic slots

        ImGui::TreePop();
    }

    // -----------------------------------------------------------------------
    // Active Haptic Slots monitor
    // -----------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Active Haptic Slots");
    if (ImGui::BeginChild("ActiveHapticsFS", ImVec2(0, 160), true)) {
        bool anyActive = false;

        // --- Constant ---
        auto active_constants = fsHaptics->GetActiveConstants();
        for (const auto& [slot, info] : active_constants) {
            if (!info.active) continue;
            anyActive = true;
            if (ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(slot + 1000)),
                                "Constant Force [slot %d]", slot)) {
                ImGui::Text("Strength: %.3f", info.strength);
                if (info.duration_ms == SDL_HAPTIC_INFINITY)
                    ImGui::Text("Duration: Infinite");
                else
                    ImGui::Text("Duration: %u ms", info.duration_ms);
                ImGui::TreePop();
            }
        }

        // --- Periodic ---
        auto active_periodics = fsHaptics->GetActivePeriodicEffects();
        for (const auto& [slot, info] : active_periodics) {
            if (!info.active) continue;
            anyActive = true;
            if (ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(slot + 2000)),
                                "Periodic (%s) [slot %d]", PeriodicTypeName(info.wave_type), slot)) {
                ImGui::Text("Wave: %s",       PeriodicTypeName(info.wave_type));
                ImGui::Text("Magnitude: %.3f", info.magnitude);
                ImGui::Text("Period: %u ms",   info.period);
                ImGui::Text("Offset: %.3f",    info.offset);
                if (info.duration_ms == SDL_HAPTIC_INFINITY)
                    ImGui::Text("Duration: Infinite");
                else
                    ImGui::Text("Duration: %u ms", info.duration_ms);
                ImGui::TreePop();
            }
        }

        // --- Conditions ---
        auto active_conditions = fsHaptics->GetActiveConditions();
        for (const auto& [slot, info] : active_conditions) {
            anyActive = true;
            if (ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(slot + 3000)),
                                "Condition [slot %d]", slot)) {
                ImGui::Text("Type: %s", ConditionTypeName(info.type));
                if (info.duration_ms == SDL_HAPTIC_INFINITY)
                    ImGui::Text("Duration: Infinite");
                else
                    ImGui::Text("Duration: %u ms", info.duration_ms);
                ImGui::Text("Center: %.3f  Deadband: %.3f", info.center, info.deadband);
                ImGui::Text("L/R Coeff: %.3f / %.3f",
                            info.left_coeff,  info.right_coeff);
                ImGui::Text("L/R Sat:   %.3f / %.3f",
                            info.left_sat,    info.right_sat);
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
}