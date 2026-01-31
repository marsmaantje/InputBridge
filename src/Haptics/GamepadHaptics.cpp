#include "GamepadHaptics.h"
#include <algorithm>

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

int GamepadHaptics::PlayLeftRight(float large_magnitude, float small_magnitude, uint32_t duration_ms) {
    RunAsync([this, large_magnitude, small_magnitude, duration_ms]() {
        if (!m_haptic) {
            // Fallback for Gamepads that don't support SDL_Haptic but support Rumble
            SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
            SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
            if (gamepad) {
                if (!SDL_RumbleGamepad(gamepad, static_cast<Uint16>(large_magnitude * 0xFFFF), static_cast<Uint16>(small_magnitude * 0xFFFF), duration_ms)) {
                    SDL_Log("SDL_RumbleGamepad failed: %s", SDL_GetError());
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
            if (!SDL_RunHapticEffect(m_haptic, m_rumbleEffectId, 1)) {
                SDL_Log("SDL_RunHapticEffect failed: %s", SDL_GetError());
            }
        } else {
            SDL_Log("UploadEffect (Rumble) failed: %s", SDL_GetError());
        }
    });
    return 0;
}
