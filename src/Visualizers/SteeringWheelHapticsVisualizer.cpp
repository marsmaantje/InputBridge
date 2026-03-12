#include "SteeringWheelHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/SteeringWheelHaptics.h"
#include "../../lib/wheel-rpm-lib/include/wheel/utils/rpm_mapper.hpp"
#include <SDL3/SDL.h>

void SteeringWheelHapticsVisualizer::Draw(const DeviceState& dev, DeviceManager& deviceManager) {
    HapticDevice *haptic = deviceManager.GetHapticDevice(dev.instance_id);
    if (haptic) {
        if (haptic->IsReady() && dev.joystick) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Haptics: Ready");
            SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(dev.joystick);
            if (sdl_haptic) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%d effect slots)", SDL_GetMaxHapticEffects(sdl_haptic));
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
