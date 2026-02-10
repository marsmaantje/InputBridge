#include "GamepadHaptics.h"
#include <algorithm>
#include <cstring>
#include <SDL3/SDL_joystick.h>

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
        if ((SDL_GetHapticFeatures(m_haptic) & SDL_HAPTIC_LEFTRIGHT) == 0) {
            // If not supported, fall back to simple rumble
            float strength = std::max(large_magnitude, small_magnitude);
            if (!SDL_PlayHapticRumble(m_haptic, strength, duration_ms)) {
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
            if (!SDL_RunHapticEffect(m_haptic, m_rumbleEffectId, (duration_ms == SDL_HAPTIC_INFINITY) ? SDL_HAPTIC_INFINITY : 1)) {
                SDL_Log("SDL_RunHapticEffect failed: %s", SDL_GetError());
            }
        } else {
            SDL_Log("UploadEffect (Rumble) failed: %s", SDL_GetError());
        }
    });
    return 0;
}
