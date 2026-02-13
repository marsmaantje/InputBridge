#include "GamepadHaptics.h"
#include "DualSenseTriggerEffectGenerator.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_gamepad.h>

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

        // Standard Gamepad Rumble (DualSense, Xbox, etc.)
        // We use SDL_RumbleGamepad directly which is thread-safe and handles DualSense correctly on SDL3.
        SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
        SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
        if (gamepad) {
            Uint16 low = static_cast<Uint16>(large_magnitude * 0xFFFF);
            Uint16 high = static_cast<Uint16>(small_magnitude * 0xFFFF);
            if (!SDL_RumbleGamepad(gamepad, low, high, duration_ms)) {
                SDL_Log("SDL_RumbleGamepad failed: %s", SDL_GetError());
            }
        } else {
            SDL_Log("SDL_GetGamepadFromID failed - gamepad not available");
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
    
    // Check SDL power info - USB devices typically report more accurate battery info
    SDL_PowerState state = SDL_POWERSTATE_UNKNOWN;
    int percent = 0;
    state = SDL_GetGamepadPowerInfo(gamepad, &percent);
    
    // USB DualSense controllers report as "wired" power state
    if (state == SDL_POWERSTATE_CHARGED || state == SDL_POWERSTATE_CHARGING) {
        return true;  // Definitely USB
    }
    
    // Check the joystick path
    const char* path = SDL_GetJoystickPath(m_joystick);
    if (path) {
        std::string pathStr(path);
        std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), 
            [](unsigned char c){ return std::tolower(c); });
        
        // USB devices have "usb" in path or "hidraw" without bluetooth identifiers
        if (pathStr.find("usb") != std::string::npos) {
            return true;
        }
        
        // Bluetooth devices have these identifiers
        if (pathStr.find("bluetooth") != std::string::npos || 
            pathStr.find("bt") != std::string::npos ||
            pathStr.find("-") != std::string::npos) {  // MAC address format AA:BB:CC:DD:EE:FF or AA-BB-CC-DD-EE-FF
            return false;
        }
        
        // On Linux, check if it's a hidraw device without BT indicators
        if (pathStr.find("hidraw") != std::string::npos) {
            return true;  // Likely USB
        }
    }
    
    // If power state is on battery, it's definitely Bluetooth
    if (state == SDL_POWERSTATE_ON_BATTERY) {
        return false;
    }
    
    // Default to USB as it's safer (simpler protocol)
    return true;
}

void GamepadHaptics::SendDualSenseTriggerEffect(uint8_t* leftTriggerData, uint8_t* rightTriggerData) {
    if (!m_joystick) {
        SDL_Log("SendDualSenseTriggerEffect: No joystick available");
        return;
    }
    
    bool isUSB = IsDualSenseUSB();
    SDL_Log("SendDualSenseTriggerEffect: %s connection detected", isUSB ? "USB" : "Bluetooth");
    
    if (isUSB) {
        // USB Report Format (Report ID 0x02)
        // Reference: Valve's ISteamDualsense and HID specifications
        Uint8 data[48] = {0};
        data[0] = 0x02;  // Report ID for USB
        data[1] = 0xFF;  // Feature flags - enable haptics, lightbar, etc.
        data[2] = 0xF7;  // Feature flags byte 2 - enable trigger motor effects
        
        // Rumble motors (set to 0 to not interfere with triggers)
        data[3] = 0;     // Right rumble
        data[4] = 0;     // Left rumble
        
        // Mute button LED (optional)
        data[9] = 0;
        
        // Right trigger effect data starts at offset 11
        std::memcpy(&data[11], rightTriggerData, 11);
        
        // Left trigger effect data starts at offset 22
        std::memcpy(&data[22], leftTriggerData, 11);
        
        // Player LED (optional, keep existing)
        data[44] = 0;
        
        SDL_Log("Sending USB trigger data: Report[0]=0x%02X, Flags[1]=0x%02X, Flags[2]=0x%02X", data[0], data[1], data[2]);
        SDL_Log("Right trigger[0]=0x%02X, Left trigger[0]=0x%02X", rightTriggerData[0], leftTriggerData[0]);
        
        // Use SDL_SendJoystickEffect instead of SDL_SendGamepadEffect
        if (!SDL_SendJoystickEffect(m_joystick, data, sizeof(data))) {
            SDL_Log("SDL_SendJoystickEffect (DualSense USB Trigger) failed: %s", SDL_GetError());
        } else {
            SDL_Log("DualSense USB trigger effect sent successfully");
        }
    } else {
        // Bluetooth Report Format (Report ID 0x31)
        // More complex format based on Sony's HID specification
        Uint8 data[78] = {0};
        data[0] = 0x31;  // Report ID for Bluetooth
        data[1] = 0x02;  // HID + CRC; bit 0-3: seq tag, bit 4: enable rumble emulation, bit 5-7: flags
        
        // Feature flags byte 1 (offset 2)
        // bit 0: enable_rumble_emulation
        // bit 1: use_rumble_not_haptics  
        // bit 2: enable_improved_rumble_emulation
        // bit 3: enable_audio_control
        // bit 4: enable_mic_mute
        // bit 5: enable_audio_mute  
        // bit 6: enable_led_color
        // bit 7: enable_unk_0x80
        data[2] = 0x14;  // Enable improved rumble (bit 2) and enable_trigger_motor_effects would be in next byte
        
        // Feature flags byte 2 (offset 3)  
        // bit 0: enable_rumble
        // bit 1: enable_led_strips
        // bit 2: enable_trigger_motor_effects  <-- THIS IS THE KEY!
        // bit 3: enable_unk_0x08
        // bit 4: enable_trigger_motor_effects_right
        // bit 5: enable_trigger_motor_effects_left
        // bit 6: enable_unk_0x40
        // bit 7: enable_unk_0x80
        data[3] = 0x3C;  // Enable trigger motors: bits 2, 3, 4, 5 (0x04 | 0x08 | 0x10 | 0x20 = 0x3C)
        
        // Rumble motors (set to 0)
        data[4] = 0;     // Right rumble
        data[5] = 0;     // Left rumble
        
        // Mute button LED
        data[10] = 0;
        
        // Right trigger effect data - Bluetooth uses different offset
        // In BT mode, the trigger data starts at offset 23 for right, 34 for left
        // This is different from USB!
        std::memcpy(&data[23], rightTriggerData, 11);
        
        // Left trigger effect data
        std::memcpy(&data[34], leftTriggerData, 11);
        
        // LED color (R, G, B)
        data[45] = 0;
        data[46] = 0;
        data[47] = 255;  // Blue
        
        // Player LED (optional)
        data[48] = 0;
        
        SDL_Log("Sending BT trigger data: Report[0]=0x%02X, Tag[1]=0x%02X, Flags1[2]=0x%02X, Flags2[3]=0x%02X", 
                data[0], data[1], data[2], data[3]);
        SDL_Log("Right trigger[0]=0x%02X, Left trigger[0]=0x%02X", rightTriggerData[0], leftTriggerData[0]);
        
        // For Bluetooth, SDL should handle CRC32 calculation
        // Use SDL_SendJoystickEffect instead of SDL_SendGamepadEffect
        if (!SDL_SendJoystickEffect(m_joystick, data, sizeof(data))) {
            SDL_Log("SDL_SendJoystickEffect (DualSense BT Trigger) failed: %s", SDL_GetError());
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
    
    {
        // Prepare trigger effect data (11 bytes each)
        uint8_t leftTriggerData[11] = {};
        uint8_t rightTriggerData[11] = {};
        
        // Determine which trigger to update
        bool updateLeft = (std::strcmp(trigger, "left") == 0 || std::strcmp(trigger, "both") == 0);
        bool updateRight = (std::strcmp(trigger, "right") == 0 || std::strcmp(trigger, "both") == 0);
        
        // Helper lambda to apply effect to a trigger data array
        auto applyEffect = [&](uint8_t* triggerData) {
            if (std::strcmp(effect_type, "off") == 0) {
                DualSenseTriggerEffectGenerator::Off(triggerData, 0);
            } 
            else if (std::strcmp(effect_type, "feedback") == 0) {
                uint8_t position = params.count("position") ? params.at("position") : 0;
                uint8_t strength = params.count("strength") ? params.at("strength") : 5;
                DualSenseTriggerEffectGenerator::Feedback(triggerData, 0, position, strength);
            } 
            else if (std::strcmp(effect_type, "weapon") == 0) {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 2;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 7;
                uint8_t strength = params.count("strength") ? params.at("strength") : 5;
                DualSenseTriggerEffectGenerator::Weapon(triggerData, 0, start_position, end_position, strength);
            } 
            else if (std::strcmp(effect_type, "vibration") == 0) {
                uint8_t position = params.count("position") ? params.at("position") : 0;
                uint8_t amplitude = params.count("amplitude") ? params.at("amplitude") : 5;
                uint8_t frequency = params.count("frequency") ? params.at("frequency") : 10;
                DualSenseTriggerEffectGenerator::Vibration(triggerData, 0, position, amplitude, frequency);
            } 
            else if (std::strcmp(effect_type, "bow") == 0) {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 0;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 8;
                uint8_t strength = params.count("strength") ? params.at("strength") : 5;
                uint8_t snap_force = params.count("snap_force") ? params.at("snap_force") : 5;
                DualSenseTriggerEffectGenerator::Bow(triggerData, 0, start_position, end_position, strength, snap_force);
            } 
            else if (std::strcmp(effect_type, "galloping") == 0) {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 0;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 9;
                uint8_t first_foot = params.count("first_foot") ? params.at("first_foot") : 2;
                uint8_t second_foot = params.count("second_foot") ? params.at("second_foot") : 7;
                uint8_t frequency = params.count("frequency") ? params.at("frequency") : 10;
                DualSenseTriggerEffectGenerator::Galloping(triggerData, 0, start_position, end_position, first_foot, second_foot, frequency);
            } 
            else if (std::strcmp(effect_type, "machine") == 0) {
                uint8_t start_position = params.count("start_position") ? params.at("start_position") : 0;
                uint8_t end_position = params.count("end_position") ? params.at("end_position") : 9;
                uint8_t amplitude_a = params.count("amplitude_a") ? params.at("amplitude_a") : 4;
                uint8_t amplitude_b = params.count("amplitude_b") ? params.at("amplitude_b") : 4;
                uint8_t frequency = params.count("frequency") ? params.at("frequency") : 10;
                uint8_t period = params.count("period") ? params.at("period") : 10;
                DualSenseTriggerEffectGenerator::Machine(triggerData, 0, start_position, end_position, amplitude_a, amplitude_b, frequency, period);
            }
            else {
                SDL_Log("Unknown DualSense trigger effect type: %s", effect_type);
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
    }
    
    return 0;
}