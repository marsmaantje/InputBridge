#include "SteeringWheelHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/SteeringWheelHaptics.h"
#include <SDL3/SDL.h>

void SteeringWheelHapticsVisualizer::Draw(const DeviceState& dev, DeviceManager& deviceManager) {
    ImGui::Separator();
    ImGui::Text("Haptics Test");

    HapticDevice *haptic = deviceManager.GetHapticDevice(dev.instance_id);
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

        if (ImGui::TreeNode("Condition (Spring)")) {
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

            if (ImGui::Button("Play Spring")) {
                wheelHaptics->PlayCondition(m_condition_right_sat, m_condition_left_sat, m_condition_right_coeff, m_condition_left_coeff, m_condition_deadband, m_condition_center,
                                            m_condition_infinite_duration ? SDL_HAPTIC_INFINITY : (uint32_t)m_condition_duration);
            }
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("Haptics not available");
    }
}
