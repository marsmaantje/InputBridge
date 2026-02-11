#include "GamepadHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/DualSenseTriggerEffectGenerator.h"
#include <SDL3/SDL.h>

void GamepadHapticsVisualizer::Draw(const DeviceState& dev, DeviceManager& deviceManager) {
    ImGui::Separator();
    ImGui::Text("Haptics Test");

    ImGui::SliderFloat("Low Freq", &m_low_freq, 0.0f, 1.0f);
    ImGui::SliderFloat("High Freq", &m_high_freq, 0.0f, 1.0f);
    ImGui::Checkbox("Infinite Duration", &m_infinite_duration);
    if (!m_infinite_duration) {
        ImGui::SliderInt("Duration (ms)", &m_duration, 0, 5000);
    }

    if (ImGui::Button("Play Rumble")) {
        HapticDevice *haptic = deviceManager.GetHapticDevice(dev.instance_id);
        if (haptic) {
            if (auto *gamepadHaptics = dynamic_cast<GamepadHaptics *>(haptic)) {
                gamepadHaptics->Rumble(m_low_freq, m_high_freq,
                                       m_infinite_duration ? SDL_HAPTIC_INFINITY : (uint32_t)m_duration);
            }
        }
    }

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

    ImGui::Separator();
    ImGui::Text("DualSense Adaptive Triggers");

    static int left_effect_type = 0;
    static int left_params[10] = {};
    static int right_effect_type = 0;
    static int right_params[10] = {};

    const char* ds_effect_names[] = { "Off", "Feedback", "Weapon", "Vibration", "Bow", "Galloping", "MachineGun" };

    auto DrawTriggerUI = [&](const char* label, int& effect_type, int* params) {
        ImGui::PushID(label);
        ImGui::Text("%s", label);
        ImGui::Combo("Effect Type", &effect_type, ds_effect_names, IM_ARRAYSIZE(ds_effect_names));

        switch (effect_type) {
            case 1: // Feedback
                ImGui::SliderInt("Start Position", &params[0], 0, 9);
                ImGui::SliderInt("Strength", &params[1], 0, 8);
                break;
            case 2: // Weapon
                ImGui::SliderInt("Start Position", &params[0], 0, 9);
                ImGui::SliderInt("End Position", &params[1], 0, 9);
                ImGui::SliderInt("Strength", &params[2], 0, 8);
                break;
            case 3: // Vibration
                ImGui::SliderInt("Position", &params[0], 0, 9);
                ImGui::SliderInt("Amplitude", &params[1], 0, 8);
                ImGui::SliderInt("Frequency", &params[2], 0, 255);
                break;
            case 4: // Bow
                ImGui::SliderInt("Start Position", &params[0], 0, 9);
                ImGui::SliderInt("End Position", &params[1], 0, 9);
                ImGui::SliderInt("Strength", &params[2], 0, 8);
                ImGui::SliderInt("Snap Force", &params[3], 0, 8);
                break;
            case 5: // Galloping
                ImGui::SliderInt("Start Position", &params[0], 0, 9);
                ImGui::SliderInt("End Position", &params[1], 0, 9);
                ImGui::SliderInt("First Foot", &params[2], 0, 9);
                ImGui::SliderInt("Second Foot", &params[3], 0, 9);
                ImGui::SliderInt("Frequency", &params[4], 0, 255);
                break;
            case 6: // MachineGun
                ImGui::SliderInt("Start Position", &params[0], 0, 9);
                ImGui::SliderInt("End Position", &params[1], 0, 9);
                ImGui::SliderInt("Strength", &params[2], 0, 8);
                ImGui::SliderInt("Frequency", &params[3], 0, 255);
                break;
        }
        ImGui::PopID();
    };

    DrawTriggerUI("Left Trigger", left_effect_type, left_params);
    ImGui::Separator();
    DrawTriggerUI("Right Trigger", right_effect_type, right_params);

    if (ImGui::Button("Send Effect")) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(dev.instance_id);
        if (pad) {
            auto GetEffect = [&](int effect_type, int* params) -> DualSenseTriggerEffect {
                switch (effect_type) {
                    default:
                    case 0: return DualSenseTriggerEffectGenerator::Off();
                    case 1: return DualSenseTriggerEffectGenerator::Feedback((uint8_t)params[0], (uint8_t)params[1]);
                    case 2: return DualSenseTriggerEffectGenerator::Weapon((uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2]);
                    case 3: return DualSenseTriggerEffectGenerator::Vibration((uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2]);
                    case 4: return DualSenseTriggerEffectGenerator::Bow((uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3]);
                    case 5: return DualSenseTriggerEffectGenerator::Galloping((uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3], (uint8_t)params[4]);
                    case 6: return DualSenseTriggerEffectGenerator::MachineGun((uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3]);
                }
            };

            DualSenseTriggerEffect left_effect = GetEffect(left_effect_type, left_params);
            DualSenseTriggerEffect right_effect = GetEffect(right_effect_type, right_params);

            // DS5 Output Report for Triggers (Report ID 0x31)
            Uint8 data[48] = {};
            data[0] = 0x31; // Report ID
            data[1] = 0x02; // Mode: Allow Trigger update
            data[2] = 0x0C; // Valid Flags: Bit 2 (Right Trigger), Bit 3 (Left Trigger)

            DualSenseTriggerEffectGenerator::ApplyToBuffer(data, 11, right_effect);
            DualSenseTriggerEffectGenerator::ApplyToBuffer(data, 22, left_effect);

            SDL_SendGamepadEffect(pad, data, sizeof(data));
        }
    }
}
