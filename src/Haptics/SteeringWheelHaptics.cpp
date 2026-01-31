#include "SteeringWheelHaptics.h"

int SteeringWheelHaptics::SetGain(int gain)
{
    RunAsync([this, gain]() {
        if (!m_haptic) {
            return;
        }
        SDL_SetHapticGain(m_haptic, gain);
    });
    return 0;
}

int SteeringWheelHaptics::PlayConstant(float strength, uint32_t duration_ms) {
    RunAsync([this, strength, duration_ms]() {
        if (!m_haptic) {
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));

        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = 1; // Play on the X axis
        effect.constant.level = static_cast<Sint16>(strength * 32767.0f);
        effect.constant.length = duration_ms;

        CreateAndRunEffect(effect, duration_ms);
    });
    return 0;
}

int SteeringWheelHaptics::PlayPeriodic(
    float strength, 
    uint32_t period, 
    float magnitude, 
    float offset, 
    uint32_t phase, 
    uint32_t duration_ms) {
    RunAsync([this, strength, period, magnitude, offset, phase, duration_ms]() {
        if (!m_haptic) {
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));

        effect.type = SDL_HAPTIC_SINE; // Using Sine as a common periodic effect
        effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.periodic.direction.dir[0] = 1;
        effect.periodic.period = period;
        effect.periodic.magnitude = static_cast<Sint16>(magnitude * 32767.0f);
        effect.periodic.offset = static_cast<Sint16>(offset * 32767.0f);
        effect.periodic.phase = phase;
        effect.periodic.length = duration_ms;

        CreateAndRunEffect(effect, duration_ms);
    });
    return 0;
}

int SteeringWheelHaptics::PlayCondition(
    float right_sat,
    float left_sat,
    float right_coeff,
    float left_coeff,
    float deadband,
    float center,
    uint32_t duration_ms
) {
    RunAsync([this, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms]() {
        if (!m_haptic) {
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));

        effect.type = SDL_HAPTIC_SPRING; // Using Spring as a common condition effect
        effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;
        
        // Condition properties are arrays for each axis
        effect.condition.right_sat[0] = static_cast<Uint16>(right_sat * 0xFFFF);
        effect.condition.left_sat[0] = static_cast<Uint16>(left_sat * 0xFFFF);
        effect.condition.right_coeff[0] = static_cast<Sint16>(right_coeff * 32767.0f);
        effect.condition.left_coeff[0] = static_cast<Sint16>(left_coeff * 32767.0f);
        effect.condition.deadband[0] = static_cast<Uint16>(deadband * 0xFFFF);
        effect.condition.center[0] = static_cast<Sint16>(center * 32767.0f);
        effect.condition.length = duration_ms;

        CreateAndRunEffect(effect, duration_ms);
    });
    return 0;
}

int SteeringWheelHaptics::CreateAndRunEffect(SDL_HapticEffect& effect, uint32_t duration_ms) {
    // Length is already set in specific effect structs
    int effect_id = SDL_CreateHapticEffect(m_haptic, &effect);
    if (effect_id < 0) {
        // Handle error
        return effect_id;
    }

    if (!SDL_RunHapticEffect(m_haptic, effect_id, 1)) {
        // Handle error
        return -1;
    }
    
    // The effect can be destroyed after it has been run
    SDL_Delay(duration_ms);
    SDL_DestroyHapticEffect(m_haptic, effect_id);

    return effect_id;
}

void SteeringWheelHaptics::UpdateEffect(int effect_id, SDL_HapticEffect& effect)
{
    if (!SDL_UpdateHapticEffect(m_haptic, effect_id, &effect)) {
        // Handle error
    }
}
