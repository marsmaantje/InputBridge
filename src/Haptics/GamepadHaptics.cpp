#include "GamepadHaptics.h"
#include <algorithm>

int GamepadHaptics::PlayLeftRight(float large_magnitude, float small_magnitude, uint32_t duration_ms) {
    RunAsync([this, large_magnitude, small_magnitude, duration_ms]() {
        if (!m_haptic) {
            return;
        }

        // Check if the haptic device supports the SDL_HAPTIC_LEFTRIGHT effect
        if ((SDL_GetHapticFeatures(m_haptic) & SDL_HAPTIC_LEFTRIGHT) == 0) {
            // If not supported, fall back to simple rumble
            float strength = std::max(large_magnitude, small_magnitude);
            SDL_PlayHapticRumble(m_haptic, strength, duration_ms);
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
            SDL_RunHapticEffect(m_haptic, m_rumbleEffectId, 1);
        }
    });
    return 0;
}
