#include "GamepadHaptics.h"
#include "DualSenseTriggerEffectGenerator.h"
#include <algorithm>
#include <cstring>
#include <SDL3/SDL_joystick.h>

using namespace ExtendInput::DataTools::DualSense;

bool GamepadHaptics::IsReady() const {
    if (HapticDevice::IsReady()) return true;

    SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
    if (gamepad) {
        // Optimistically return true if we have a gamepad handle.
        // Some controllers (like Steam Controller via HIDAPI) might not report capabilities
        // via properties correctly but still support rumble commands.
        return true;
    }
    return false;
}

int GamepadHaptics::Rumble(float large_magnitude, float small_magnitude, uint32_t duration_ms) {
    RunAsync([this, large_magnitude, small_magnitude, duration_ms]() {
        // Special handling for Steam Controller
        Uint16 vendor = SDL_GetJoystickVendor(m_joystick);
        Uint16 product = SDL_GetJoystickProduct(m_joystick);
        const char* name = SDL_GetJoystickName(m_joystick);

        // Check for Valve Steam Controller (VID 0x28DE, PIDs 0x1102, 0x1142)
        bool isSteamController = (vendor == 0x28DE && (product == 0x1102 || product == 0x1142));
        if (!isSteamController && name && std::strstr(name, "Steam Controller")) {
            isSteamController = true;
        }

        if (isSteamController) {
            // The Steam Controller haptics are not traditional rumble motors.
            // They are linear resonant actuators in the trackpads.
            // We can send a haptic pulse command via SDL_SendJoystickEffect.

            // Define necessary structs from controller_structs.h to avoid include path issues
            #pragma pack(push, 1)
            typedef struct {
                unsigned char type;
                unsigned char length;
            } FeatureReportHeader;

            typedef struct {
                unsigned char which_pad;
                unsigned short pulse_duration;
                unsigned short pulse_interval;
                unsigned short pulse_count;
                short dBgain;
                unsigned char priority;
            } MsgFireHapticPulse;

            typedef struct {
                FeatureReportHeader header;
                union {
                    MsgFireHapticPulse fireHapticPulse;
                } payload;
            } FeatureReportMsg;
            #pragma pack(pop)

            const int ID_FIRE_HAPTIC_PULSE = 11;

            auto send_pulse = [&](unsigned char pad, float magnitude) {
                if (magnitude <= 0.0f) return;

                unsigned char buf[65] = {0};
                buf[0] = 0x87; // Feature Report ID for Steam Controller
                FeatureReportMsg *msg = (FeatureReportMsg *)&buf[1];

                const unsigned short pulse_duration_us = static_cast<unsigned short>(std::clamp(magnitude, 0.0f, 1.0f) * 2000.0f); // 0-2ms
                const unsigned short pulse_interval_us = 3000; // 3ms
                const unsigned short single_pulse_cycle_ms = 5; // A reasonable cycle time for one pulse

                unsigned short pulse_count = 1;
                if (duration_ms > single_pulse_cycle_ms) {
                    pulse_count = duration_ms / single_pulse_cycle_ms;
                }
                if (pulse_count == 0) pulse_count = 1;

                msg->header.type = ID_FIRE_HAPTIC_PULSE;
                msg->header.length = sizeof(MsgFireHapticPulse);
                msg->payload.fireHapticPulse = {pad, pulse_duration_us, pulse_interval_us, pulse_count, 0, 0};

                if (!SDL_SendJoystickEffect(m_joystick, buf, sizeof(buf))) {
                    SDL_Log("SDL_SendJoystickEffect (Steam Controller Pad %d) failed: %s", pad, SDL_GetError());
                }
            };

            // Map large_magnitude to left pad, small_magnitude to right pad.
            send_pulse(0, large_magnitude); // 0 for left pad
            send_pulse(1, small_magnitude); // 1 for right pad
            return;
        }

        if (!m_haptic) {
            // Fallback for Gamepads that don't support SDL_Haptic but support Rumble
            SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
            SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
            if (gamepad) {
                if (!SDL_RumbleGamepad(gamepad, static_cast<Uint16>(large_magnitude * 0xFFFF), static_cast<Uint16>(small_magnitude * 0xFFFF), duration_ms)) {
                    SDL_Log("SDL_RumbleGamepad failed (VID 0x%04X, PID 0x%04X): %s", vendor, product, SDL_GetError());
                }
            } else {
                SDL_Log("GamepadHaptics: Could not get gamepad handle for ID %u", id);
            }
            return;
        }

        // Check if the haptic device supports the SDL_HAPTIC_LEFTRIGHT effect
        if ((SDL_GetHapticFeatures(m_haptic.Get()) & SDL_HAPTIC_LEFTRIGHT) == 0) {
            // If not supported, fall back to simple rumble
            float strength = std::max(large_magnitude, small_magnitude);
            if (!SDL_PlayHapticRumble(m_haptic.Get(), strength, duration_ms)) {
                SDL_Log("SDL_PlayHapticRumble failed: %s", SDL_GetError());
            }
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect)); // Zero out the effect struct

        effect.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = duration_ms;
        effect.leftright.large_magnitude = static_cast<Uint16>(std::clamp(large_magnitude, 0.0f, 1.0f) * 0xFFFF);
        effect.leftright.small_magnitude = static_cast<Uint16>(std::clamp(small_magnitude, 0.0f, 1.0f) * 0xFFFF);

        // Create or update the effect
        m_rumbleEffectId = UploadEffect(effect, m_rumbleEffectId);
        if (m_rumbleEffectId >= 0) {
            if (!SDL_RunHapticEffect(m_haptic.Get(), m_rumbleEffectId, (duration_ms == SDL_HAPTIC_INFINITY) ? SDL_HAPTIC_INFINITY : 1)) {
                SDL_Log("SDL_RunHapticEffect failed: %s", SDL_GetError());
            }
        } else {
            SDL_Log("UploadEffect (Rumble) failed: %s", SDL_GetError());
        }
    });
    return 0;
}

bool GamepadHaptics::IsDualSense() const {
    Uint16 vendor = SDL_GetJoystickVendor(m_joystick);
    Uint16 product = SDL_GetJoystickProduct(m_joystick);
    
    // Sony DualSense (PS5) - VID: 0x054C, PID: 0x0CE6
    // Sony DualSense Edge - VID: 0x054C, PID: 0x0DF2
    return (vendor == 0x054C && (product == 0x0CE6 || product == 0x0DF2));
}

bool GamepadHaptics::IsDualSenseUSB() const {
    if (!IsDualSense()) return false;
    
    // Check connection type via SDL property
    SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
    if (!gamepad) return false;
    
    // USB DualSense controllers typically use "USB" in their path or have specific properties
    // For now, we'll use a heuristic: check if it's a HID device path
    const char* path = SDL_GetJoystickPath(m_joystick);
    if (path) {
        // USB devices typically have "usb" in their path
        // Bluetooth devices have "bluetooth" or longer MAC-style addresses
        std::string pathStr(path);
        std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), ::tolower);
        
        if (pathStr.find("usb") != std::string::npos) {
            return true;
        }
        if (pathStr.find("bluetooth") != std::string::npos || pathStr.find("bt") != std::string::npos) {
            return false;
        }
    }
    
    // Fallback: assume Bluetooth (safer default as it has more complex protocol)
    return false;
}

void GamepadHaptics::SendDualSenseTriggerEffect(uint8_t* leftTriggerData, uint8_t* rightTriggerData) {
    SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
    SDL_Gamepad* pad = SDL_GetGamepadFromID(id);
    if (!pad) {
        SDL_Log("GamepadHaptics::SendDualSenseTriggerEffect: Could not get gamepad handle");
        return;
    }
    
    bool isUSB = IsDualSenseUSB();
    
    if (isUSB) {
        // USB Report Format (Report ID 0x02)
        // Simpler format, 48 bytes
        Uint8 data[48] = {};
        data[0] = 0x02;  // Report ID for USB
        data[1] = 0xFF;  // Feature flags - all features enabled
        data[2] = 0xF7;  // Feature flags byte 2
        
        // Right trigger starts at offset 11
        std::memcpy(&data[11], rightTriggerData, 11);
        // Left trigger starts at offset 22
        std::memcpy(&data[22], leftTriggerData, 11);
        
        if (!SDL_SendGamepadEffect(pad, data, sizeof(data))) {
            SDL_Log("SDL_SendGamepadEffect (DualSense USB Trigger) failed: %s", SDL_GetError());
        } else {
            SDL_Log("DualSense USB trigger effect sent successfully");
        }
    } else {
        // Bluetooth Report Format (Report ID 0x31)
        // More complex, requires proper structure and CRC
        Uint8 data[78] = {};  // Using minimal BT report size
        data[0] = 0x31;  // Report ID for Bluetooth
        data[1] = 0x02;  // seq_tag | (enable_rumble_emulation << 4) | (use_rumble_not_haptics << 5)
        
        // Feature flags - enable trigger motors
        data[2] = 0x1C;  // enable_improved_rumble_emulation | enable_audio_control | enable_led_color | enable_trigger_motor_effects
        
        // Right trigger at offset 11 (same as USB offset in the feature report)
        std::memcpy(&data[11], rightTriggerData, 11);
        // Left trigger at offset 22
        std::memcpy(&data[22], leftTriggerData, 11);
        
        // For Bluetooth, we should calculate CRC32, but SDL might handle this
        // Send the report
        if (!SDL_SendGamepadEffect(pad, data, sizeof(data))) {
            SDL_Log("SDL_SendGamepadEffect (DualSense BT Trigger) failed: %s", SDL_GetError());
        } else {
            SDL_Log("DualSense Bluetooth trigger effect sent successfully");
        }
    }
}

int GamepadHaptics::SendDualSenseTrigger(const char* trigger, const char* effect_type, const std::map<std::string, int>& params) {
    if (!IsDualSense()) {
        SDL_Log("SendDualSenseTrigger: Not a DualSense controller");
        return -1;
    }
    
    RunAsync([this, trigger = std::string(trigger), effect_type = std::string(effect_type), params]() {
        // Prepare trigger effect data (11 bytes each)
        uint8_t leftTriggerData[11] = {};
        uint8_t rightTriggerData[11] = {};
        
        // Determine which trigger to update
        bool updateLeft = (trigger == "left" || trigger == "both");
        bool updateRight = (trigger == "right" || trigger == "both");
        
        // Helper lambda to apply effect to a trigger data array
        auto applyEffect = [&](uint8_t* triggerData) {
            if (effect_type == "off") {
                DualSenseTriggerEffectGenerator::Off(triggerData, 0);
            } 
            else if (effect_type == "feedback") {
                uint8_t position = params.count("position") ? params.at("position") : 0;
                uint8_t strength = params.count("strength") ? params.at("strength") : 5;
                DualSenseTriggerEffectGenerator::Feedback(triggerData, 0, position, strength);
            } 
            else if (effect_type == "weapon") {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 2;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 7;
                uint8_t strength = params.count("strength") ? params.at("strength") : 5;
                DualSenseTriggerEffectGenerator::Weapon(triggerData, 0, start_position, end_position, strength);
            } 
            else if (effect_type == "vibration") {
                uint8_t position = params.count("position") ? params.at("position") : 0;
                uint8_t amplitude = params.count("amplitude") ? params.at("amplitude") : 5;
                uint8_t frequency = params.count("frequency") ? params.at("frequency") : 10;
                DualSenseTriggerEffectGenerator::Vibration(triggerData, 0, position, amplitude, frequency);
            } 
            else if (effect_type == "bow") {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 0;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 8;
                uint8_t strength = params.count("strength") ? params.at("strength") : 5;
                uint8_t snap_force = params.count("snap_force") ? params.at("snap_force") : 5;
                DualSenseTriggerEffectGenerator::Bow(triggerData, 0, start_position, end_position, strength, snap_force);
            } 
            else if (effect_type == "galloping") {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 0;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 9;
                uint8_t first_foot = params.count("first_foot") ? params.at("first_foot") : 2;
                uint8_t second_foot = params.count("second_foot") ? params.at("second_foot") : 7;
                uint8_t frequency = params.count("frequency") ? params.at("frequency") : 10;
                DualSenseTriggerEffectGenerator::Galloping(triggerData, 0, start_position, end_position, first_foot, second_foot, frequency);
            } 
            else if (effect_type == "machine") {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 0;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 9;
                uint8_t amplitude_a = params.count("amplitude_a") ? params.at("amplitude_a") : 4;
                uint8_t amplitude_b = params.count("amplitude_b") ? params.at("amplitude_b") : 4;
                uint8_t frequency = params.count("frequency") ? params.at("frequency") : 10;
                uint8_t period = params.count("period") ? params.at("period") : 10;
                DualSenseTriggerEffectGenerator::Machine(triggerData, 0, start_position, end_position, amplitude_a, amplitude_b, frequency, period);
            }
            else {
                SDL_Log("Unknown DualSense trigger effect type: %s", effect_type.c_str());
                DualSenseTriggerEffectGenerator::Off(triggerData, 0);
            }
        };
        
        // Apply effect to the appropriate trigger(s)
        if (updateLeft) {
            applyEffect(leftTriggerData);
        }
        if (updateRight) {
            applyEffect(rightTriggerData);
        }
        
        // Send the effect
        SendDualSenseTriggerEffect(leftTriggerData, rightTriggerData);
    });
    
    return 0;
}
