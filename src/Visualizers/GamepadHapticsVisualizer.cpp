#include "GamepadHapticsVisualizer.h"
#include "imgui.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/DualSenseTriggerEffectGenerator.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <string>

using namespace ExtendInput::DataTools::DualSense;

void GamepadHapticsVisualizer::Draw(const DeviceState& dev, DeviceManager& deviceManager) {
    HapticDevice* haptic = deviceManager.GetHapticDevice(dev.instance_id);
    if (haptic) {
        if (haptic->IsReady()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Haptics: Ready");
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

    ImGui::SliderFloat("Low Freq", &m_low_freq, 0.0f, 1.0f);
    ImGui::SliderFloat("High Freq", &m_high_freq, 0.0f, 1.0f);
    ImGui::Checkbox("Infinite Duration", &m_infinite_duration);
    if (!m_infinite_duration) {
        ImGui::SliderInt("Duration (ms)", &m_duration, 0, 5000);
    }

    if (ImGui::Button("Play Rumble")) {
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

    const char* ds_effect_names[] = { "Off", "Feedback", "Weapon", "Vibration", "Bow", "Galloping", "Machine" };

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
            case 6: // Machine
                ImGui::SliderInt("Start Position", &params[0], 0, 9);
                ImGui::SliderInt("End Position", &params[1], 0, 9);
                ImGui::SliderInt("Amplitude A", &params[2], 0, 7);
                ImGui::SliderInt("Amplitude B", &params[3], 0, 7);
                ImGui::SliderInt("Frequency", &params[4], 0, 255);
                ImGui::SliderInt("Period", &params[5], 0, 255);
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
            SDL_Joystick* joystick = SDL_GetGamepadJoystick(pad);
            if (!joystick) return;
            
            // Check if it's a DualSense controller
            Uint16 vendor = SDL_GetJoystickVendor(joystick);
            Uint16 product = SDL_GetJoystickProduct(joystick);
            bool isDualSense = (vendor == 0x054C && (product == 0x0CE6 || product == 0x0DF2));
            
            if (!isDualSense) {
                SDL_Log("Not a DualSense controller - trigger effects not supported");
                return;
            }
            
            // Detect USB vs Bluetooth
            bool isUSB = false;
            const char* path = SDL_GetJoystickPath(joystick);
            if (path) {
                std::string pathStr(path);
                std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), ::tolower);
                if (pathStr.find("usb") != std::string::npos) {
                    isUSB = true;
                }
            }
            
            SDL_Log("DualSense controller detected - Connection: %s", isUSB ? "USB" : "Bluetooth");
            
            // Prepare trigger data (11 bytes each)
            uint8_t leftTriggerData[11] = {};
            uint8_t rightTriggerData[11] = {};

            auto ApplyEffect = [&](int effect_type, int* params, uint8_t* triggerData) {
                switch (effect_type) {
                    default:
                    case 0: DualSenseTriggerEffectGenerator::Off(triggerData, 0); break;
                    case 1: DualSenseTriggerEffectGenerator::Feedback(triggerData, 0, (uint8_t)params[0], (uint8_t)params[1]); break;
                    case 2: DualSenseTriggerEffectGenerator::Weapon(triggerData, 0, (uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2]); break;
                    case 3: DualSenseTriggerEffectGenerator::Vibration(triggerData, 0, (uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2]); break;
                    case 4: DualSenseTriggerEffectGenerator::Bow(triggerData, 0, (uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3]); break;
                    case 5: DualSenseTriggerEffectGenerator::Galloping(triggerData, 0, (uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3], (uint8_t)params[4]); break;
                    case 6: DualSenseTriggerEffectGenerator::Machine(triggerData, 0, (uint8_t)params[0], (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3], (uint8_t)params[4], (uint8_t)params[5]); break;
                }
            };

            ApplyEffect(right_effect_type, right_params, rightTriggerData);
            ApplyEffect(left_effect_type, left_params, leftTriggerData);
            
            if (isUSB) {
                // USB Report Format (Report ID 0x02)
                Uint8 data[48] = {};
                data[0] = 0x02;  // Report ID for USB
                data[1] = 0xFF;  // Feature flags - all features enabled
                data[2] = 0xF7;  // Feature flags byte 2
                
                // Right trigger starts at offset 11
                std::memcpy(&data[11], rightTriggerData, 11);
                // Left trigger starts at offset 22
                std::memcpy(&data[22], leftTriggerData, 11);
                
                if (!SDL_SendGamepadEffect(pad, data, sizeof(data))) {
                    SDL_Log("SDL_SendGamepadEffect (USB) failed: %s", SDL_GetError());
                } else {
                    SDL_Log("DualSense USB trigger effect sent successfully");
                }
            } else {
                // Bluetooth Report Format (Report ID 0x31)
                Uint8 data[78] = {};
                data[0] = 0x31;  // Report ID for Bluetooth
                data[1] = 0x02;  // seq_tag
                data[2] = 0x1C;  // Feature flags
                
                // Right trigger at offset 11
                std::memcpy(&data[11], rightTriggerData, 11);
                // Left trigger at offset 22
                std::memcpy(&data[22], leftTriggerData, 11);
                
                if (!SDL_SendGamepadEffect(pad, data, sizeof(data))) {
                    SDL_Log("SDL_SendGamepadEffect (Bluetooth) failed: %s", SDL_GetError());
                } else {
                    SDL_Log("DualSense Bluetooth trigger effect sent successfully");
                }
            }
        }
    }
}
