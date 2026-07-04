#include "App/Log.h"
#include "FlightStickHaptics.h"
#include <algorithm>

static constexpr const char* kTag = "FlightStickHaptics";

// ---------------------------------------------------------------------------
// PlayConstant
// ---------------------------------------------------------------------------

int FlightStickHaptics::PlayConstant(int slot, float strength, uint32_t duration_ms) {
    RunAsync([this, slot, strength, duration_ms]() {
        if (!m_haptic) {
            LOG_WARN(kTag, "PlayConstant - Haptic device not ready");
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

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_constantEffects[slot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR(kTag, "PlayConstant - Run failed: %s", SDL_GetError());
                }
            }
            {
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

int FlightStickHaptics::PlayPeriodic(int slot, HapticPeriodicType wave_type, float strength, uint32_t period,
                                     float magnitude, float offset, uint32_t phase,
                                     uint32_t duration_ms) {
    RunAsync([this, slot, wave_type, strength, period, magnitude, offset, phase, duration_ms]() {
        if (!m_haptic) {
            LOG_WARN(kTag, "PlayPeriodic - Haptic device not ready");
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));

        // Square wave: SDL3 has no native SQUARE type; synthesise it as a
        // SDL_HAPTIC_CUSTOM effect with two equal-duration samples (high, low).
        // Each sample lasts period/2 ms, giving a 50 % duty-cycle square wave
        // with the requested period.
        if (wave_type == HapticPeriodicType::Square) {
            // Clamp to [-1, 1] before scaling so SDL Sint16 never overflows.
            const auto hi_val = std::clamp( magnitude + offset, -1.0f, 1.0f);
            const auto lo_val = std::clamp(-magnitude + offset, -1.0f, 1.0f);
            // SDL_HapticCustom::data is Uint16* but values are treated as Sint16.
            Uint16 wave_data[2] = {
                static_cast<Uint16>(static_cast<Sint16>(hi_val * 32767.0f)),
                static_cast<Uint16>(static_cast<Sint16>(lo_val * 32767.0f))
            };
            const Uint16 half_period = static_cast<Uint16>(std::max(1u, period / 2u));

            effect.type = SDL_HAPTIC_CUSTOM;
            effect.custom.direction.type   = SDL_HAPTIC_CARTESIAN;
            effect.custom.direction.dir[0] = 1;
            effect.custom.direction.dir[1] = 0;
            effect.custom.channels         = 1;
            effect.custom.period           = half_period;
            effect.custom.samples          = 2;
            effect.custom.data             = wave_data;  // copied by SDL
            effect.custom.length           = duration_ms;
        } else {
            effect.type = ToSDLPeriodicType(wave_type);  // translate once, here
            effect.periodic.direction.type   = SDL_HAPTIC_CARTESIAN;
            effect.periodic.direction.dir[0] = 1;
            effect.periodic.direction.dir[1] = 0;
            effect.periodic.period    = static_cast<Uint16>(period);
            effect.periodic.magnitude = static_cast<Sint16>(magnitude * 32767.0f);
            effect.periodic.offset    = static_cast<Sint16>(offset * 32767.0f);
            effect.periodic.phase     = static_cast<Uint16>(phase);
            effect.periodic.length    = duration_ms;
        }

        SDL_HapticEffectID existing = -1;
        auto it = m_periodicEffects.find(slot);
        if (it != m_periodicEffects.end()) existing = it->second;

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_periodicEffects[slot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR(kTag, "PlayPeriodic - Run failed: %s", SDL_GetError());
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info        = m_activePeriodicEffects[slot];
                info.wave_type    = wave_type;
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
// PlayRumble - simulated via a low-frequency periodic (no dedicated motors)
// ---------------------------------------------------------------------------

int FlightStickHaptics::PlayRumble(int slot, float large_magnitude,
                                   float small_magnitude, uint32_t duration_ms) {
    const float strength = std::max(large_magnitude, small_magnitude);
    if (strength <= 0.0f) {
        return StopPeriodic(slot);
    }
    // ~33 Hz square-ish vibration - a noticeable impact feel on force-feedback sticks.
    return PlayPeriodic(slot, HapticPeriodicType::Sine, strength, /*period_ms=*/30, strength, 0.0f, 0, duration_ms);
}

// ---------------------------------------------------------------------------
// PlayCondition
// ---------------------------------------------------------------------------

int FlightStickHaptics::PlayCondition(int slot, HapticConditionType type,
                                      float right_sat, float left_sat,
                                      float right_coeff, float left_coeff,
                                      float deadband, float center,
                                      uint32_t duration_ms) {
    RunAsync([this, slot, type, right_sat, left_sat,
              right_coeff, left_coeff, deadband, center, duration_ms]() {
        if (!m_haptic) {
            LOG_WARN(kTag, "PlayCondition - Haptic device not ready");
            return;
        }

        {
            int max_effects = SDL_GetMaxHapticEffects(m_haptic.Get());
            if (slot < 0 || slot >= max_effects) {
                LOG_WARN(kTag, "PlayCondition - Invalid slot %d (max: %d)",
                        slot, max_effects);
                return;
            }
        }

        const Uint16 sdlType = ToSDLConditionType(type);  // translate once, here

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = sdlType;
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

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_conditionEffects[slot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR(kTag, "PlayCondition - Run failed for type %s: %s",
                            ConditionTypeName(type), SDL_GetError());
                    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                    m_activeConditions.erase(slot);
                    return;
                }
            }
            {
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
