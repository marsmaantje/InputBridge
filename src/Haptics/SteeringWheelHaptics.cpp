#include "App/Log.h"
#include "SteeringWheelHaptics.h"
#include <algorithm>

int SteeringWheelHaptics::SetGain(int gain) {
    RunAsync([this, gain]() {
        if (!m_haptic) return;
        SDL_SetHapticGain(m_haptic.Get(), gain);
    });
    return 0;
}

int SteeringWheelHaptics::PlayConstant(int slot, float strength, uint32_t duration_ms) {
    RunAsync([this, slot, strength, duration_ms]() {
        if (!m_haptic) {
            LOG_WARN("SteeringWheelHaptics", "PlayConstant - Haptic device not ready");
            return;
        }

        const float clamped = std::clamp(strength, -1.0f, 1.0f);

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = -1; // X axis
        effect.constant.level = static_cast<Sint16>(clamped * 32767.0f);
        effect.constant.length = duration_ms;

        SDL_HapticEffectID existing = -1;
        auto it = m_constantEffects.find(slot);
        if (it != m_constantEffects.end()) existing = it->second;

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_constantEffects[slot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR("SteeringWheelHaptics", "PlayConstant - Run failed: %s", SDL_GetError());
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info = m_activeConstants[slot];
                info.strength     = clamped;
                info.duration_ms  = duration_ms;
                info.last_updated = SDL_GetTicks();
                info.active       = true;
            }
        }
    });
    return 0;
}

int SteeringWheelHaptics::StopConstant(int slot) {
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

int SteeringWheelHaptics::PlayPeriodic(int slot, HapticPeriodicType wave_type, float strength, uint32_t period, float magnitude, float offset, uint32_t phase, uint32_t duration_ms) {
    RunAsync([this, slot, wave_type, strength, period, magnitude, offset, phase, duration_ms]() {
        if (!m_haptic) {
            LOG_WARN("SteeringWheelHaptics", "PlayPeriodic - Haptic device not ready");
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = ToSDLPeriodicType(wave_type);  // translate once, here
        effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.periodic.direction.dir[0] = 1;
        effect.periodic.period    = static_cast<Uint16>(period);
        effect.periodic.magnitude = static_cast<Sint16>(magnitude * 32767.0f);
        effect.periodic.offset    = static_cast<Sint16>(offset * 32767.0f);
        effect.periodic.phase     = static_cast<Uint16>(phase);
        effect.periodic.length    = duration_ms;

        SDL_HapticEffectID existing = -1;
        auto it = m_periodicEffects.find(slot);
        if (it != m_periodicEffects.end()) existing = it->second;

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_periodicEffects[slot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR("SteeringWheelHaptics", "PlayPeriodic - Run failed: %s", SDL_GetError());
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info = m_activePeriodicEffects[slot];
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

int SteeringWheelHaptics::StopPeriodic(int slot) {
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

int SteeringWheelHaptics::PlayRumble(int slot, float large_magnitude, float small_magnitude, uint32_t duration_ms) {
    // Steering wheels have no rumble motors; simulate with a low-freq sine wave.
    const float strength = std::max(large_magnitude, small_magnitude);
    if (strength <= 0.0f) {
        return StopPeriodic(slot);
    }
    // 50 ms period = 20 Hz — a recognisable vibration feel.
    return PlayPeriodic(slot, HapticPeriodicType::Sine, strength, 50, strength, 0.0f, 0, duration_ms);
}

int SteeringWheelHaptics::PlayCondition(int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, uint32_t duration_ms) {
    RunAsync([this, slot, type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms]() {
        if (!m_haptic) {
            LOG_WARN("SteeringWheelHaptics", "PlayCondition - Haptic device not ready");
            return;
        }

        {
            int max_effects = SDL_GetMaxHapticEffects(m_haptic.Get());
            if (slot < 0 || slot >= max_effects) {
                LOG_WARN("SteeringWheelHaptics", "PlayCondition - Invalid slot %d (max: %d)", slot, max_effects);
                return;
            }
        }

        const Uint16 sdlType = ToSDLConditionType(type);  // translate once, here

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = sdlType;
        effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;
        effect.condition.right_sat[0]   = static_cast<Uint16>(right_sat   * 65535.0f);
        effect.condition.left_sat[0]    = static_cast<Uint16>(left_sat    * 65535.0f);
        effect.condition.right_coeff[0] = static_cast<Sint16>(right_coeff * 32767.0f);
        effect.condition.left_coeff[0]  = static_cast<Sint16>(left_coeff  * 32767.0f);
        effect.condition.deadband[0]    = static_cast<Uint16>(deadband    * 65535.0f);
        effect.condition.center[0]      = static_cast<Sint16>(center      * 32767.0f);
        effect.condition.length = duration_ms;

        SDL_HapticEffectID existing = -1;
        auto it = m_conditionEffects.find(slot);
        if (it != m_conditionEffects.end()) existing = it->second;

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_conditionEffects[slot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR("SteeringWheelHaptics", "PlayCondition - Run failed for type %s: %s",
                            ConditionTypeName(type), SDL_GetError());
                    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                    m_activeConditions.erase(slot);
                    return;
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info = m_activeConditions[slot];
                info.type        = type;
                info.right_sat   = right_sat;
                info.left_sat    = left_sat;
                info.right_coeff = right_coeff;
                info.left_coeff  = left_coeff;
                info.deadband    = deadband;
                info.center      = center;
                info.duration_ms = duration_ms;
                info.last_updated = SDL_GetTicks();
            }
        }
    });
    return 0;
}

int SteeringWheelHaptics::StopCondition(int slot) {
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

void SteeringWheelHaptics::StopAll() {
    HapticDevice::StopAll();
}