#include "SteeringWheelHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/SteeringWheelHaptics.h"
#include "wheel/utils/rpm_mapper.hpp"
#include <SDL3/SDL.h>

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

                int num_currently_playing = 0;
                if (auto *wheelHaptics = dynamic_cast<SteeringWheelHaptics *>(haptic)) {
                    if (wheelHaptics->GetActiveConstant().active) {
                        num_currently_playing++;
                    }
                    if (wheelHaptics->GetActivePeriodic().active) {
                        num_currently_playing++;
                    }
                    num_currently_playing += (int)wheelHaptics->GetActiveConditions().size();
                }

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
        ImGui::SameLine();
        /*
        if (ImGui::Button("Reset Device")) {
            haptic->Close();
            if (!haptic->Init().IsOk()) {
                SDL_Log("SteeringWheelHapticsVisualizer: Reset Device failed to re-initialize haptics");
            }
            if (dev.gamepad) {
                SDL_RumbleGamepad(dev.gamepad, 0, 0, 0);
                SDL_RumbleGamepadTriggers(dev.gamepad, 0, 0, 0);
            }
        }
        */
    } else {
        ImGui::TextDisabled("Haptics not available");
    }

    ImGui::Separator();
    ImGui::Text("Haptics Test");

    if (auto *wheelHaptics = dynamic_cast<SteeringWheelHaptics *>(haptic)) {
        if (ImGui::TreeNode("Constant Force")) {
            ImGui::SliderFloat("Strength", &m_constant_strength, -1.0f, 1.0f);
            ImGui::Checkbox("Infinite Duration", &m_constant_infinite_duration);
            if (!m_constant_infinite_duration) {
                ImGui::SliderInt("Duration (ms)", &m_constant_duration, 0, 5000);
            }
            if (ImGui::Button("Play Constant")) {
                wheelHaptics->PlayConstant(m_constant_strength, m_constant_infinite_duration ? SDL_HAPTIC_INFINITY : (uint32_t)m_constant_duration);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Periodic (Sine)")) {
            ImGui::SliderFloat("Strength", &m_periodic_strength, 0.0f, 1.0f);
            ImGui::SliderInt("Period (ms)", &m_periodic_period, 1, 5000);
            ImGui::SliderFloat("Magnitude", &m_periodic_magnitude, 0.0f, 1.0f);
            ImGui::SliderFloat("Offset", &m_periodic_offset, -1.0f, 1.0f);
            ImGui::SliderInt("Phase", &m_periodic_phase, 0, 36000);
            ImGui::Checkbox("Infinite Duration", &m_periodic_infinite_duration);
            if (!m_periodic_infinite_duration) {
                ImGui::SliderInt("Duration (ms)", &m_periodic_duration, 0, 5000);
            }

            if (ImGui::Button("Play Periodic")) {
                wheelHaptics->PlayPeriodic(m_periodic_strength, (uint32_t)m_periodic_period, m_periodic_magnitude, m_periodic_offset, (uint32_t)m_periodic_phase,
                                            m_periodic_infinite_duration ? SDL_HAPTIC_INFINITY : (uint32_t)m_periodic_duration);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Condition Effects")) {
            const char* condition_types[] = { "Spring", "Damper", "Inertia", "Friction" };
            ImGui::Combo("Type", &m_condition_type, condition_types, IM_ARRAYSIZE(condition_types));
            ImGui::Separator();

            int max_slots = 1;
            if (dev.joystick) {
                SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(dev.joystick);
                if (sdl_haptic) {
                    max_slots = SDL_GetMaxHapticEffects(sdl_haptic);
                    SDL_CloseHaptic(sdl_haptic);
                }
            }
            ImGui::SliderInt("Slot", &m_condition_slot, 0, max_slots > 0 ? max_slots - 1 : 0);

            ImGui::SliderFloat("Right Sat", &m_condition_right_sat, 0.0f, 1.0f);
            ImGui::SliderFloat("Left Sat", &m_condition_left_sat, 0.0f, 1.0f);
            ImGui::SliderFloat("Right Coeff", &m_condition_right_coeff, -1.0f, 1.0f);
            ImGui::SliderFloat("Left Coeff", &m_condition_left_coeff, -1.0f, 1.0f);
            ImGui::SliderFloat("Deadband", &m_condition_deadband, 0.0f, 1.0f);
            ImGui::SliderFloat("Center", &m_condition_center, -1.0f, 1.0f);
            ImGui::Checkbox("Infinite Duration", &m_condition_infinite_duration);
            if (!m_condition_infinite_duration) {
                ImGui::SliderInt("Duration (ms)", &m_condition_duration, 0, 10000);
            }

            if (ImGui::Button("Play Condition")) {
                uint16_t sdl_type = SDL_HAPTIC_SPRING;
                switch (m_condition_type) {
                    case 0: sdl_type = SDL_HAPTIC_SPRING; break;
                    case 1: sdl_type = SDL_HAPTIC_DAMPER; break;
                    case 2: sdl_type = SDL_HAPTIC_INERTIA; break;
                    case 3: sdl_type = SDL_HAPTIC_FRICTION; break;
                }
                wheelHaptics->PlayCondition(m_condition_slot, sdl_type, m_condition_right_sat, m_condition_left_sat, m_condition_right_coeff, m_condition_left_coeff, m_condition_deadband, m_condition_center,
                                            m_condition_infinite_duration ? SDL_HAPTIC_INFINITY : (uint32_t)m_condition_duration);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop Condition")) {
                wheelHaptics->StopCondition(m_condition_slot);
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::Text("Active Haptic Slots");
        if (ImGui::BeginChild("ActiveHaptics", ImVec2(0, 150), true)) {
            bool anyActive = false;

            // --- Constant ---
            auto active_constant = wheelHaptics->GetActiveConstant();
            if (active_constant.active) {
                anyActive = true;
                if (ImGui::TreeNodeEx("Constant Force", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Strength: %.3f", active_constant.strength);
                    if (active_constant.duration_ms == SDL_HAPTIC_INFINITY)
                        ImGui::Text("Duration: Infinite");
                    else
                        ImGui::Text("Duration: %u ms", active_constant.duration_ms);
                    ImGui::TreePop();
                }
            }

            // --- Periodic ---
            auto active_periodic = wheelHaptics->GetActivePeriodic();
            if (active_periodic.active) {
                anyActive = true;
                if (ImGui::TreeNodeEx("Periodic (Sine)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Strength: %.3f", active_periodic.strength);
                    ImGui::Text("Period: %u ms", active_periodic.period);
                    ImGui::Text("Magnitude: %.3f", active_periodic.magnitude);
                    ImGui::Text("Offset: %.3f", active_periodic.offset);
                    ImGui::Text("Phase: %u", active_periodic.phase);
                    if (active_periodic.duration_ms == SDL_HAPTIC_INFINITY)
                        ImGui::Text("Duration: Infinite");
                    else
                        ImGui::Text("Duration: %u ms", active_periodic.duration_ms);
                    ImGui::TreePop();
                }
            }

            // --- Conditions ---
            auto active_conditions = wheelHaptics->GetActiveConditions();
            for (const auto& [slot, info] : active_conditions) {
                anyActive = true;
                if (ImGui::TreeNode((void*)(intptr_t)slot, "Slot %d", slot)) {
                    const char* type_str = "Unknown";
                    switch (info.type) {
                        case SDL_HAPTIC_SPRING: type_str = "Spring"; break;
                        case SDL_HAPTIC_DAMPER: type_str = "Damper"; break;
                        case SDL_HAPTIC_INERTIA: type_str = "Inertia"; break;
                        case SDL_HAPTIC_FRICTION: type_str = "Friction"; break;
                    }
                    ImGui::Text("Type: %s", type_str);
                    if (info.duration_ms == SDL_HAPTIC_INFINITY) {
                        ImGui::Text("Duration: Infinite");
                    } else {
                        ImGui::Text("Duration: %u ms", info.duration_ms);
                    }
                    ImGui::Text("Center: %.3f", info.center);
                    ImGui::Text("Deadband: %.3f", info.deadband);
                    ImGui::Text("L/R Coeff: %.3f / %.3f", info.left_coeff, info.right_coeff);
                    ImGui::Text("L/R Sat: %.3f / %.3f", info.left_sat, info.right_sat);
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

            // Show the wheel name and a preview of the LED bar.
            ImGui::Text("%s", wheel->name().c_str());
            ImGui::SameLine();

            // Draw a simple LED bar preview using RPMMapper.
            constexpr int kPreviewLEDs = 10;
            auto leds = wheel::RPMMapper::linear(m_rpm_percent, kPreviewLEDs);
            for (int i = 0; i < kPreviewLEDs; ++i) {
                ImGui::SameLine();
                ImVec4 col = leds[i]
                    ? ImVec4(1.0f, 0.4f, 0.0f, 1.0f)   // active  – orange
                    : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);  // inactive – dark
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