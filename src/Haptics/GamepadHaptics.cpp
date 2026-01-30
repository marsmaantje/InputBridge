#include "GamepadHaptics.h"

int GamepadHaptics::PlayLeftRight(float large_magnitude, float small_magnitude, uint32_t duration_ms) {
    if (!m_haptic) {
        return -1;
    }

    // Check if the haptic device supports the SDL_HAPTIC_LEFTRIGHT effect
    if ((SDL_GetHapticFeatures(m_haptic) & SDL_HAPTIC_LEFTRIGHT) == 0) {
        // If not supported, fall back to simple rumble or handle the error
        // For now, we'll try a simple rumble with the large motor
        return SDL_PlayHapticRumble(m_haptic, large_magnitude, duration_ms) ? 0 : -1;
    }

    SDL_HapticEffect effect;
    SDL_memset(&effect, 0, sizeof(SDL_HapticEffect)); // Zero out the effect struct

    effect.type = SDL_HAPTIC_LEFTRIGHT;
    effect.leftright.length = duration_ms;
    effect.leftright.large_magnitude = static_cast<Uint16>(large_magnitude * 0xFFFF);
    effect.leftright.small_magnitude = static_cast<Uint16>(small_magnitude * 0xFFFF);

    // Create the effect
    int effect_id = SDL_CreateHapticEffect(m_haptic, &effect);
    if (effect_id < 0) {
        return -1; // Failed to create the effect
    }

    // Run the effect
    if (!SDL_RunHapticEffect(m_haptic, effect_id, 1)) {
        SDL_DestroyHapticEffect(m_haptic, effect_id); // Clean up
        return -1; // Failed to run the effect
    }

    // The effect will play for 'duration_ms'. 
    // In a real-world scenario, you might want to manage effect lifetimes
    // differently rather than blocking with SDL_Delay.
    // For this implementation, we assume the effect plays and is then forgotten.
    // To allow the effect to complete, we can delay and then destroy.
    SDL_Delay(duration_ms);
    SDL_DestroyHapticEffect(m_haptic, effect_id);

    return 0; // Success
}
