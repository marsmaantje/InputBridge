#include "FlightStickHaptics.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// PlayConstant
// ---------------------------------------------------------------------------

int FlightStickHaptics::PlayConstant(int slot, float strength, uint32_t duration_ms) {
    RunAsync([this, slot, strength, duration_ms]() {
        if (!m_haptic) {
            SDL_Log("FlightStickHaptics::PlayConstant - Haptic device not ready");
            return;
        }

        const float clamped = std::clamp(strength, -1.0f, 1.0f);

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = SDL_HAPTIC_CONSTANT;
        // Use a Cartesian direction so negative strength pushes the other way.
        effect.constant.direction.type   = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = -1; // X axis
        effect.constant.direction.dir[1] =  0; // Y axis
        effect.constant.level            = static_cast<Sint16>(clamped * 32767.0f);
        effect.constant.length           = duration_ms;

        SDL_HapticEffectID existing = -1;
        auto it = m_constantEffects.find(slot);
        if (it != m_constantEffects.end()) existing = it->second;

        SDL_HapticEffectID newId = UploadEffect(effect, existing);
        if (newId != -1) {
            m_constantEffects[slot] = newId;
            if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                SDL_Log("FlightStickHaptics::PlayConstant - Run failed: %s", SDL_GetError());
            } else {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info        = m_activeConstants[slot];
                info.strength     = clamped;
                info.duration_ms  = duration_ms;
                info.last_updated = SDL_GetTicks();
                info.active       = true;
            }
        }
    });
    return 0;
}

int FlightStickHaptics::StopConstant(int slot) {
    RunAsync([this, slot]() {
        if (!m_haptic) return;
        auto it = m_constantEffects.find(slot);
        if (it != m_constantEffects.end() && it->second != -1) {
            SDL_StopHapticEffect(m_haptic.Get(), it->second);
            SDL_DestroyHapticEffect(m_haptic.Get(), it->second);
            m_constantEffects.erase(it);
            std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
            m_activeConstants.erase(slot);
        }
    });
    return 0;
}

// ---------------------------------------------------------------------------
// PlayPeriodic
// ---------------------------------------------------------------------------

int FlightStickHaptics::PlayPeriodic(int slot, float strength, uint32_t period,
                                     float magnitude, float offset, uint32_t phase,
                                     uint32_t duration_ms) {
    RunAsync([this, slot, strength, period, magnitude, offset, phase, duration_ms]() {
        if (!m_haptic) {
            SDL_Log("FlightStickHaptics::PlayPeriodic - Haptic device not ready");
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction.type   = SDL_HAPTIC_CARTESIAN;
        effect.periodic.direction.dir[0] = 1;
        effect.periodic.direction.dir[1] = 0;
        effect.periodic.period    = static_cast<Uint16>(period);
        effect.periodic.magnitude = static_cast<Sint16>(magnitude * 32767.0f);
        effect.periodic.offset    = static_cast<Sint16>(offset * 32767.0f);
        effect.periodic.phase     = static_cast<Uint16>(phase);
        effect.periodic.length    = duration_ms;

        SDL_HapticEffectID existing = -1;
        auto it = m_periodicEffects.find(slot);
        if (it != m_periodicEffects.end()) existing = it->second;

        SDL_HapticEffectID newId = UploadEffect(effect, existing);
        if (newId != -1) {
            m_periodicEffects[slot] = newId;
            if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                SDL_Log("FlightStickHaptics::PlayPeriodic - Run failed: %s", SDL_GetError());
            } else {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info        = m_activePeriodicEffects[slot];
                info.strength     = strength;
                info.period       = period;
                info.magnitude    = magnitude;
                info.offset       = offset;
                info.phase        = phase;
                info.duration_ms  = duration_ms;
                info.last_updated = SDL_GetTicks();
                info.active       = true;
            }
        }
    });
    return 0;
}

int FlightStickHaptics::StopPeriodic(int slot) {
    RunAsync([this, slot]() {
        if (!m_haptic) return;
        auto it = m_periodicEffects.find(slot);
        if (it != m_periodicEffects.end() && it->second != -1) {
            SDL_StopHapticEffect(m_haptic.Get(), it->second);
            SDL_DestroyHapticEffect(m_haptic.Get(), it->second);
            m_periodicEffects.erase(it);
            std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
            m_activePeriodicEffects.erase(slot);
        }
    });
    return 0;
}

// ---------------------------------------------------------------------------
// PlayRumble — simulated via a low-frequency periodic (no dedicated motors)
// ---------------------------------------------------------------------------

int FlightStickHaptics::PlayRumble(int slot, float large_magnitude,
                                   float small_magnitude, uint32_t duration_ms) {
    const float strength = std::max(large_magnitude, small_magnitude);
    if (strength <= 0.0f) {
        return StopPeriodic(slot);
    }
    // ~33 Hz square-ish vibration — a noticeable impact feel on force-feedback sticks.
    return PlayPeriodic(slot, strength, /*period_ms=*/30, strength, 0.0f, 0, duration_ms);
}

// ---------------------------------------------------------------------------
// PlayCondition
// ---------------------------------------------------------------------------

int FlightStickHaptics::PlayCondition(int slot, uint16_t type,
                                      float right_sat, float left_sat,
                                      float right_coeff, float left_coeff,
                                      float deadband, float center,
                                      uint32_t duration_ms) {
    RunAsync([this, slot, type, right_sat, left_sat,
              right_coeff, left_coeff, deadband, center, duration_ms]() {
        if (!m_haptic) {
            SDL_Log("FlightStickHaptics::PlayCondition - Haptic device not ready");
            return;
        }

        {
            int max_effects = SDL_GetMaxHapticEffects(m_haptic.Get());
            if (slot < 0 || slot >= max_effects) {
                SDL_Log("FlightStickHaptics::PlayCondition - Invalid slot %d (max: %d)",
                        slot, max_effects);
                return;
            }
        }

        if (type != SDL_HAPTIC_SPRING   && type != SDL_HAPTIC_DAMPER &&
            type != SDL_HAPTIC_INERTIA  && type != SDL_HAPTIC_FRICTION) {
            SDL_Log("FlightStickHaptics::PlayCondition - Invalid condition type: %d", type);
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = type;
        effect.condition.direction.type   = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;
        effect.condition.direction.dir[1] = 0;
        effect.condition.length = duration_ms;

        // Apply parameters on both the X (primary) and Y (secondary) axes so
        // that the condition is felt on pitch as well as roll on a 2-axis stick.
        for (int axis = 0; axis < 2; ++axis) {
            effect.condition.right_sat[axis]   = static_cast<Uint16>(right_sat   * 65535.0f);
            effect.condition.left_sat[axis]    = static_cast<Uint16>(left_sat    * 65535.0f);
            effect.condition.right_coeff[axis] = static_cast<Sint16>(right_coeff * 32767.0f);
            effect.condition.left_coeff[axis]  = static_cast<Sint16>(left_coeff  * 32767.0f);
            effect.condition.deadband[axis]    = static_cast<Uint16>(deadband    * 65535.0f);
            effect.condition.center[axis]      = static_cast<Sint16>(center      * 32767.0f);
        }

        SDL_HapticEffectID existing = -1;
        auto it = m_conditionEffects.find(slot);
        if (it != m_conditionEffects.end()) existing = it->second;

        SDL_HapticEffectID newId = UploadEffect(effect, existing);
        if (newId != -1) {
            m_conditionEffects[slot] = newId;
            if (SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info        = m_activeConditions[slot];
                info.type         = type;
                info.right_sat    = right_sat;
                info.left_sat     = left_sat;
                info.right_coeff  = right_coeff;
                info.left_coeff   = left_coeff;
                info.deadband     = deadband;
                info.center       = center;
                info.duration_ms  = duration_ms;
                info.last_updated = SDL_GetTicks();
            } else {
                SDL_Log("FlightStickHaptics::PlayCondition - Run failed for type %d: %s",
                        type, SDL_GetError());
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                m_activeConditions.erase(slot);
            }
        }
    });
    return 0;
}

int FlightStickHaptics::StopCondition(int slot) {
    RunAsync([this, slot]() {
        if (!m_haptic) return;
        auto it = m_conditionEffects.find(slot);
        if (it != m_conditionEffects.end() && it->second != -1) {
            SDL_StopHapticEffect(m_haptic.Get(), it->second);
            SDL_DestroyHapticEffect(m_haptic.Get(), it->second);
            m_conditionEffects.erase(it);
            std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
            m_activeConditions.erase(slot);
        }
    });
    return 0;
}

// ---------------------------------------------------------------------------
// StopAll
// ---------------------------------------------------------------------------

void FlightStickHaptics::StopAll() {
    HapticDevice::StopAll();
}
