#include "App/Log.h"
#include "GamepadHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/GamepadHaptics.h"
#include <SDL3/SDL.h>

void GamepadHapticsVisualizer::Draw(const DeviceState& dev, DeviceManager& deviceManager) {
    HapticDevice* haptic = deviceManager.GetHapticDevice(dev.instance_id);
    if (haptic) {
        if (haptic->IsReady() && dev.joystick) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Haptics: Ready");
            SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(dev.joystick);
            if (sdl_haptic) {
                ImGui::SameLine();
                int max_effects = SDL_GetMaxHapticEffects(sdl_haptic);
                int max_playing = SDL_GetMaxHapticEffectsPlaying(sdl_haptic);
                ImGui::TextDisabled("(%d slots, %d max playing)", max_effects, max_playing);
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
        ImGui::TextDisabled("Haptics: Not Supported");
    }

    ImGui::Separator();
    ImGui::Text("Haptics Test");

    ImGui::SliderInt("Slot##rumble", &m_rumble_slot, 0, 7);
    ImGui::SliderFloat("Low Freq", &m_low_freq, 0.0f, 1.0f);
    ImGui::SliderFloat("High Freq", &m_high_freq, 0.0f, 1.0f);
    ImGui::Checkbox("Infinite Duration", &m_infinite_duration);
    if (!m_infinite_duration) {
        ImGui::SliderInt("Duration (ms)", &m_duration, 0, 5000);
    }

    if (ImGui::Button("Play Rumble")) {
        if (haptic) {
            if (auto *gamepadHaptics = dynamic_cast<GamepadHaptics *>(haptic)) {
                gamepadHaptics->PlayRumble(m_rumble_slot, m_low_freq, m_high_freq,
                                           m_infinite_duration ? SDL_HAPTIC_INFINITY : (uint32_t)m_duration);
            }
        }
    }

    if (auto* gamepadHaptics = dynamic_cast<GamepadHaptics*>(haptic)) {
        ImGui::Separator();
        ImGui::Text("Active Haptic Effects");
        if (ImGui::BeginChild("ActiveHaptics", ImVec2(0, 150), true)) {
            bool anyActive = false;

            // --- Rumble (per-slot) ---
            auto active_rumbles = gamepadHaptics->GetActiveRumbles();
            for (const auto& [slot, info] : active_rumbles) {
                if (!info.active) continue;
                anyActive = true;
                if (ImGui::TreeNodeEx((void*)(intptr_t)(slot + 1000), ImGuiTreeNodeFlags_DefaultOpen,
                                     "Rumble [slot %d]", slot)) {
                    ImGui::Text("Large Motor: %.3f", info.large_magnitude);
                    ImGui::Text("Small Motor: %.3f", info.small_magnitude);
                    if (info.duration_ms == SDL_HAPTIC_INFINITY)
                        ImGui::Text("Duration: Infinite");
                    else
                        ImGui::Text("Duration: %u ms", info.duration_ms);
                    ImGui::TreePop();
                }
            }

            // --- DualSense Triggers ---
            if (gamepadHaptics->IsDualSense()) {
                auto active_triggers = gamepadHaptics->GetActiveDualSenseTriggers();
                if (!active_triggers.empty()) {
                    anyActive = true;
                    if (ImGui::TreeNodeEx("DualSense Triggers", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& [trigger_name, info] : active_triggers) {
                            if (ImGui::TreeNode(trigger_name.c_str())) {
                                ImGui::Text("Effect: %s", info.effect_type.c_str());
                                for (const auto& [param_name, val] : info.params) {
                                    ImGui::Text("%s: %d", param_name.c_str(), val);
                                }
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TreePop();
                    }
                }
            }

            // --- Constant force (per-slot) ---
            auto active_constants = gamepadHaptics->GetActiveConstants();
            for (const auto& [slot, info] : active_constants) {
                if (!info.active) continue;
                anyActive = true;
                if (ImGui::TreeNodeEx((void*)(intptr_t)(slot + 2000), ImGuiTreeNodeFlags_DefaultOpen,
                                     "Constant Force [slot %d]", slot)) {
                    ImGui::Text("Strength: %.3f", info.strength);
                    if (info.duration_ms == SDL_HAPTIC_INFINITY)
                        ImGui::Text("Duration: Infinite");
                    else
                        ImGui::Text("Duration: %u ms", info.duration_ms);
                    ImGui::TreePop();
                }
            }


            if (!anyActive) {
                ImGui::TextDisabled("No active haptic effects.");
            }
        }
        ImGui::EndChild();
    }

    // Check for trigger rumble capability
    {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(dev.instance_id);
        if (pad && SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false)) {
            ImGui::Separator();
            ImGui::Text("Trigger Rumble");

            ImGui::SliderInt("Left Trigger", &m_left_trigger, 0, 65535);
            ImGui::SliderInt("Right Trigger", &m_right_trigger, 0, 65535);
            ImGui::SliderInt("Trig Duration (ms)", &m_trigger_duration, 0, 5000);

            if (ImGui::Button("Play Trigger Rumble")) {
                SDL_RumbleGamepadTriggers(pad, (Uint16)m_left_trigger, (Uint16)m_right_trigger, (Uint32)m_trigger_duration);
            }
        }
    }

    // Only show DualSense UI if it's actually a DualSense controller
    bool isDualSense = false;
    if (haptic) {
        if (auto* gamepadHaptics = dynamic_cast<GamepadHaptics*>(haptic))
            isDualSense = gamepadHaptics->IsDualSense();
    }

    /***
     * TODO: Add adaptive trigger support later down the line again
     *
    if (isDualSense) {
        ImGui::Separator();
        ImGui::Text("DualSense Adaptive Triggers");

        static int left_effect_type = 0;
        static int left_params[10] = {};
        static int right_effect_type = 0;
        static int right_params[10] = {};

        const char* ds_effect_names[] = { "Off", "Feedback", "Weapon", "Vibration", "Bow", "Galloping", "Machine" };

        auto DrawTriggerUI = [&](const char* label, int& effect_type, int* params) {
            ImGui::PushID(label);
            ImGui::Text("%s", label);
            ImGui::Combo("Effect Type", &effect_type, ds_effect_names, IM_ARRAYSIZE(ds_effect_names));

            switch (effect_type) {
                case 1: // Feedback
                    ImGui::SliderInt("Position", &params[0], 0, 9);
                    ImGui::SliderInt("Strength", &params[1], 0, 8);
                    break;
                case 2: // Weapon
                    ImGui::SliderInt("Start Position", &params[0], 2, 7);
                    ImGui::SliderInt("End Position", &params[1], 0, 8);
                    ImGui::SliderInt("Strength", &params[2], 0, 8);
                    break;
                case 3: // Vibration
                    ImGui::SliderInt("Position", &params[0], 0, 9);
                    ImGui::SliderInt("Amplitude", &params[1], 0, 8);
                    ImGui::SliderInt("Frequency", &params[2], 0, 255);
                    break;
                case 4: // Bow
                    ImGui::SliderInt("Start Position", &params[0], 0, 8);
                    ImGui::SliderInt("End Position", &params[1], 0, 8);
                    ImGui::SliderInt("Strength", &params[2], 0, 8);
                    ImGui::SliderInt("Snap Force", &params[3], 0, 8);
                    break;
                case 5: // Galloping
                    ImGui::SliderInt("Start Position", &params[0], 0, 9);
                    ImGui::SliderInt("End Position", &params[1], 0, 9);
                    ImGui::SliderInt("First Foot", &params[2], 0, 6);
                    ImGui::SliderInt("Second Foot", &params[3], 0, 7);
                    ImGui::SliderInt("Frequency", &params[4], 0, 255);
                    break;
                case 6: // Machine
                    ImGui::SliderInt("Start Position", &params[0], 0, 9);
                    ImGui::SliderInt("End Position", &params[1], 0, 9);
                    ImGui::SliderInt("Amplitude A", &params[2], 0, 7);
                    ImGui::SliderInt("Amplitude B", &params[3], 0, 7);
                    ImGui::SliderInt("Frequency", &params[4], 0, 255);
                    ImGui::SliderInt("Period", &params[5], 0, 2);
                    break;
            }
            ImGui::PopID();
        };

        DrawTriggerUI("Left Trigger", left_effect_type, left_params);
        ImGui::Separator();
        DrawTriggerUI("Right Trigger", right_effect_type, right_params);

        if (ImGui::Button("Send Effect")) {
            if (auto *gamepadHaptics = dynamic_cast<GamepadHaptics *>(haptic)) {
                const char* effect_names[] = { "off", "feedback", "weapon", "vibration", "bow", "galloping", "machine" }; // must match ds_effect_names
                
                // Send left trigger effect
                std::map<std::string, int> leftParams;
                std::string leftEffectName = effect_names[left_effect_type];
                
                switch (left_effect_type) {
                    case 1: // Feedback
                        leftParams["position"] = left_params[0];
                        leftParams["strength"] = left_params[1];
                        break;
                    case 2: // Weapon
                        leftParams["start_position"] = left_params[0];
                        leftParams["end_position"] = left_params[1];
                        leftParams["strength"] = left_params[2];
                        break;
                    case 3: // Vibration
                        leftParams["position"] = left_params[0];
                        leftParams["amplitude"] = left_params[1];
                        leftParams["frequency"] = left_params[2];
                        break;
                    case 4: // Bow
                        leftParams["start_position"] = left_params[0];
                        leftParams["end_position"] = left_params[1];
                        leftParams["strength"] = left_params[2];
                        leftParams["snap_force"] = left_params[3];
                        break;
                    case 5: // Galloping
                        leftParams["start_position"] = left_params[0];
                        leftParams["end_position"] = left_params[1];
                        leftParams["first_foot"] = left_params[2];
                        leftParams["second_foot"] = left_params[3];
                        leftParams["frequency"] = left_params[4];
                        break;
                    case 6: // Machine
                        leftParams["start_position"] = left_params[0];
                        leftParams["end_position"] = left_params[1];
                        leftParams["amplitude_a"] = left_params[2];
                        leftParams["amplitude_b"] = left_params[3];
                        leftParams["frequency"] = left_params[4];
                        leftParams["period"] = left_params[5];
                        break;
                }
                
                int result = gamepadHaptics->PlayDualSenseTrigger("left", leftEffectName, leftParams);
                LOG_DEBUG("GamepadHapticsViz", "Left trigger effect '%s' result: %d", leftEffectName.c_str(), result);
                
                // Send right trigger effect
                std::map<std::string, int> rightParams;
                std::string rightEffectName = effect_names[right_effect_type];
                
                switch (right_effect_type) {
                    case 1: // Feedback
                        rightParams["position"] = right_params[0];
                        rightParams["strength"] = right_params[1];
                        break;
                    case 2: // Weapon
                        rightParams["start_position"] = right_params[0];
                        rightParams["end_position"] = right_params[1];
                        rightParams["strength"] = right_params[2];
                        break;
                    case 3: // Vibration
                        rightParams["position"] = right_params[0];
                        rightParams["amplitude"] = right_params[1];
                        rightParams["frequency"] = right_params[2];
                        break;
                    case 4: // Bow
                        rightParams["start_position"] = right_params[0];
                        rightParams["end_position"] = right_params[1];
                        rightParams["strength"] = right_params[2];
                        rightParams["snap_force"] = right_params[3];
                        break;
                    case 5: // Galloping
                        rightParams["start_position"] = right_params[0];
                        rightParams["end_position"] = right_params[1];
                        rightParams["first_foot"] = right_params[2];
                        rightParams["second_foot"] = right_params[3];
                        rightParams["frequency"] = right_params[4];
                        break;
                    case 6: // Machine
                        rightParams["start_position"] = right_params[0];
                        rightParams["end_position"] = right_params[1];
                        rightParams["amplitude_a"] = right_params[2];
                        rightParams["amplitude_b"] = right_params[3];
                        rightParams["frequency"] = right_params[4];
                        rightParams["period"] = right_params[5];
                        break;
                }
                
                result = gamepadHaptics->PlayDualSenseTrigger("right", rightEffectName, rightParams);
                LOG_DEBUG("GamepadHapticsViz", "Right trigger effect '%s' result: %d", rightEffectName.c_str(), result);
            }
        }
    }
    */
}