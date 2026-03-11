#include "SteeringWheelHaptics.h"

int SteeringWheelHaptics::SetGain(int gain) {
    RunAsync([this, gain]() {
        if (!m_haptic) {
            return;
        }
        SDL_SetHapticGain(m_haptic.Get(), gain);
    });
    return 0;
}

int SteeringWheelHaptics::PlayConstant(float strength, uint32_t duration_ms) {
    RunAsync([this, strength, duration_ms]() {
        if (!m_haptic) {
            SDL_Log("SteeringWheelHaptics::PlayConstant - Haptic device not ready");
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        // clamp strength between -1 and 1
        float clamped_strength = (strength > 1.0f) ? 1.0f : ((strength < -1.0f) ? -1.0f : strength);

        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = -1; // Play on the X axis
        effect.constant.level = static_cast<Sint16>(clamped_strength * 32767.0f);
        effect.constant.length = duration_ms;

        m_constantEffectId = UploadEffect(effect, m_constantEffectId);
        if (m_constantEffectId != -1) {
            if (!SDL_RunHapticEffect(m_haptic.Get(), m_constantEffectId, 1)) {
                SDL_Log("SteeringWheelHaptics::PlayConstant - Run failed: %s", SDL_GetError());
            }
        }
    });
    return 0;
}

int SteeringWheelHaptics::PlayPeriodic(float strength, uint32_t period, float magnitude, float offset, uint32_t phase, uint32_t duration_ms) {
    RunAsync([this, strength, period, magnitude, offset, phase, duration_ms]() {
        if (!m_haptic) {
            SDL_Log("SteeringWheelHaptics::PlayPeriodic - Haptic device not ready");
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

        m_periodicEffectId = UploadEffect(effect, m_periodicEffectId);
        if (m_periodicEffectId != -1) {
            if (!SDL_RunHapticEffect(m_haptic.Get(), m_periodicEffectId, 1)) {
                SDL_Log("SteeringWheelHaptics::PlayPeriodic - Run failed: %s", SDL_GetError());
            }
        }
    });
    return 0;
}

int SteeringWheelHaptics::PlayCondition(int slot, uint16_t type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, uint32_t duration_ms) {
    RunAsync([this, slot, type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms]() {
        if (!m_haptic) {
            SDL_Log("SteeringWheelHaptics::PlayCondition - Haptic device not ready");
            return;
        }

        if (m_joystick) {
            SDL_Haptic* sdl_haptic = SDL_OpenHapticFromJoystick(m_joystick);
            if (sdl_haptic) {
                int max_effects = SDL_GetMaxHapticEffects(sdl_haptic);
                SDL_CloseHaptic(sdl_haptic);
                if (slot < 0 || slot >= max_effects) {
                    SDL_Log("SteeringWheelHaptics::PlayCondition - Invalid slot %d (max: %d)", slot, max_effects);
                    return;
                }
            }
        }

        if (type != SDL_HAPTIC_SPRING && type != SDL_HAPTIC_DAMPER &&
            type != SDL_HAPTIC_INERTIA && type != SDL_HAPTIC_FRICTION) {
            SDL_Log("SteeringWheelHaptics::PlayCondition - Invalid condition type: %d", type);
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));

        effect.type = type;
        effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;

        // Condition properties are arrays for each axis
        effect.condition.right_sat[0] = static_cast<Uint16>(right_sat * 65535.0f);
        effect.condition.left_sat[0] = static_cast<Uint16>(left_sat * 65535.0f);
        effect.condition.right_coeff[0] = static_cast<Sint16>(right_coeff * 32767.0f);
        effect.condition.left_coeff[0] = static_cast<Sint16>(left_coeff * 32767.0f);
        effect.condition.deadband[0] = static_cast<Uint16>(deadband * 65535.0f);
        effect.condition.center[0] = static_cast<Sint16>(center * 32767.0f);
        effect.condition.length = duration_ms;

        SDL_HapticEffectID existingId = -1;
        if (m_conditionEffects.count(slot)) {
            existingId = m_conditionEffects[slot];
        }
 
        SDL_HapticEffectID newId = UploadEffect(effect, existingId);
        if (newId != -1) {
            m_conditionEffects[slot] = newId;
            if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                SDL_Log("SteeringWheelHaptics::PlayCondition - Run failed for type %d: %s", type, SDL_GetError());
            }
        }
    });
    return 0;
}

int SteeringWheelHaptics::StopCondition(int slot) {
    RunAsync([this, slot]() {
        if (!m_haptic) {
            SDL_Log("SteeringWheelHaptics::StopCondition - Haptic device not ready");
            return;
        }
        if (m_conditionEffects.count(slot)) {
            SDL_HapticEffectID effectId = m_conditionEffects.at(slot);
            if (effectId != -1) {
                SDL_StopHapticEffect(m_haptic.Get(), effectId);
                SDL_DestroyHapticEffect(m_haptic.Get(), effectId);
                m_conditionEffects.erase(slot);
            }
        }
    });
    return 0;
}
