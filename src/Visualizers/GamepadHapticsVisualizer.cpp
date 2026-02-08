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
    
    const char* ds_effect_names[] = { "Off", "Feedback", "Weapon", "Vibration", "Bow", "Galloping", "MachineGun" };
    ImGui::Combo("Effect Type", &m_ds_effect_type, ds_effect_names, IM_ARRAYSIZE(ds_effect_names));

    switch (m_ds_effect_type) {
        case 1: // Feedback
            ImGui::SliderInt("Start Position", &m_ds_params[0], 0, 9);
            ImGui::SliderInt("Strength", &m_ds_params[1], 0, 8);
            break;
        case 2: // Weapon
            ImGui::SliderInt("Start Position", &m_ds_params[0], 0, 9);
            ImGui::SliderInt("End Position", &m_ds_params[1], 0, 9);
            ImGui::SliderInt("Strength", &m_ds_params[2], 0, 8);
            break;
        case 3: // Vibration
            ImGui::SliderInt("Position", &m_ds_params[0], 0, 9);
            ImGui::SliderInt("Amplitude", &m_ds_params[1], 0, 8);
            ImGui::SliderInt("Frequency", &m_ds_params[2], 0, 255);
            break;
        case 4: // Bow
            ImGui::SliderInt("Start Position", &m_ds_params[0], 0, 9);
            ImGui::SliderInt("End Position", &m_ds_params[1], 0, 9);
            ImGui::SliderInt("Strength", &m_ds_params[2], 0, 8);
            ImGui::SliderInt("Snap Force", &m_ds_params[3], 0, 8);
            break;
        case 5: // Galloping
            ImGui::SliderInt("Start Position", &m_ds_params[0], 0, 9);
            ImGui::SliderInt("End Position", &m_ds_params[1], 0, 9);
            ImGui::SliderInt("First Foot", &m_ds_params[2], 0, 9);
            ImGui::SliderInt("Second Foot", &m_ds_params[3], 0, 9);
            ImGui::SliderInt("Frequency", &m_ds_params[4], 0, 255);
            break;
        case 6: // MachineGun
            ImGui::SliderInt("Start Position", &m_ds_params[0], 0, 9);
            ImGui::SliderInt("End Position", &m_ds_params[1], 0, 9);
            ImGui::SliderInt("Strength", &m_ds_params[2], 0, 8);
            ImGui::SliderInt("Frequency", &m_ds_params[3], 0, 255);
            break;
    }
    
    if (ImGui::Button("Send Effect")) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(dev.instance_id);
        if (pad) {
            DualSenseTriggerEffect effect;
            switch (m_ds_effect_type) {
                default:
                case 0: effect = DualSenseTriggerEffectGenerator::Off(); break;
                case 1: effect = DualSenseTriggerEffectGenerator::Feedback((uint8_t)m_ds_params[0], (uint8_t)m_ds_params[1]); break;
                case 2: effect = DualSenseTriggerEffectGenerator::Weapon((uint8_t)m_ds_params[0], (uint8_t)m_ds_params[1], (uint8_t)m_ds_params[2]); break;
                case 3: effect = DualSenseTriggerEffectGenerator::Vibration((uint8_t)m_ds_params[0], (uint8_t)m_ds_params[1], (uint8_t)m_ds_params[2]); break;
                case 4: effect = DualSenseTriggerEffectGenerator::Bow((uint8_t)m_ds_params[0], (uint8_t)m_ds_params[1], (uint8_t)m_ds_params[2], (uint8_t)m_ds_params[3]); break;
                case 5: effect = DualSenseTriggerEffectGenerator::Galloping((uint8_t)m_ds_params[0], (uint8_t)m_ds_params[1], (uint8_t)m_ds_params[2], (uint8_t)m_ds_params[3], (uint8_t)m_ds_params[4]); break;
                case 6: effect = DualSenseTriggerEffectGenerator::MachineGun((uint8_t)m_ds_params[0], (uint8_t)m_ds_params[1], (uint8_t)m_ds_params[2], (uint8_t)m_ds_params[3]); break;
            }

            // DS5 Output Report for Triggers (Report ID 0x31)
            Uint8 data[48] = {};
            data[0] = 0x31; // Report ID
            data[1] = 0x02; // Mode: Allow Trigger update
            data[2] = 0x0C; // Valid Flags: Bit 2 (Right Trigger), Bit 3 (Left Trigger)
            
            DualSenseTriggerEffectGenerator::ApplyToBuffer(data, 11, effect);
            DualSenseTriggerEffectGenerator::ApplyToBuffer(data, 22, effect);

            SDL_SendGamepadEffect(pad, data, sizeof(data));
        }
    }
}
