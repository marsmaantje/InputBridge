#include "App/Log.h"
#include "HapticDevice.h"
#include "SDL3/SDL_haptic.h"
#include <algorithm>

HapticDevice::HapticDevice(SDL_Joystick* joystick) : m_joystick(joystick) {}

HapticDevice::~HapticDevice() {
    Close();
}

InputBridge::Result<bool, InputBridge::HapticError> HapticDevice::Init() {
    if (!m_joystick) {
        LOG_ERROR("HapticDevice", "Init - Failed: Device not found (joystick is null)");
        return InputBridge::Result<bool, InputBridge::HapticError>::Err(InputBridge::HapticError::DeviceNotFound);
    }

    auto joystickID = SDL_GetJoystickID(m_joystick);
    bool isHaptic = SDL_IsJoystickHaptic(m_joystick);
    LOG_INFO("HapticDevice", "Init - Joystick ID %u, IsHaptic: %d", joystickID, isHaptic);

    if (isHaptic) {
        m_haptic.Reset(SDL_OpenHapticFromJoystick(m_joystick));
        if (!m_haptic) {
            LOG_WARN("HapticDevice", "Warning: SDL_OpenHapticFromJoystick failed: %s", SDL_GetError());
            LOG_INFO("HapticDevice", "Will continue - gamepad rumble fallback may still work, but advanced haptics will NOT work.");
        } else {
            if (SDL_HapticRumbleSupported(m_haptic.Get())) {
                if (!SDL_InitHapticRumble(m_haptic.Get())) {
                    LOG_WARN("HapticDevice", "Warning: SDL_InitHapticRumble failed: %s", SDL_GetError());
                }
            }
            SDL_SetHapticGain(m_haptic.Get(), 100);
            SDL_SetHapticAutocenter(m_haptic.Get(), 0);
        }
    }

    if (m_haptic || SDL_IsGamepad(joystickID)) {
        m_running = true;
        m_thread = std::thread(&HapticDevice::ThreadLoop, this);
    }

    if (m_haptic || SDL_IsGamepad(joystickID)) {
        LOG_INFO("HapticDevice", "Init - Success");
        return InputBridge::Result<bool, InputBridge::HapticError>::Ok(true);
    }

    LOG_ERROR("HapticDevice", "Init - Failed: Unsupported device type");
    return InputBridge::Result<bool, InputBridge::HapticError>::Err(InputBridge::HapticError::UnsupportedEffect);
}

bool HapticDevice::IsReady() const {
    return static_cast<bool>(m_haptic);
}

void HapticDevice::Close() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();

    if (m_haptic) {
        SDL_StopHapticEffects(m_haptic.Get());
    }
    {
        std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
        m_activeConstants.clear();
        m_activePeriodicEffects.clear();
        m_activeConditions.clear();
        m_activeRumbles.clear();
        m_activeDualSenseTriggers.clear();
    }
    m_haptic.Reset();  // SDL_CloseHaptic cleans up all remaining effect slots
    m_constantEffects.clear();
    m_periodicEffects.clear();
    m_rumbleEffects.clear();
    m_conditionEffects.clear();
}

void HapticDevice::RunAsync(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_tasks.push(std::move(task));
    }
    m_cv.notify_one();
}

void HapticDevice::ThreadLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // Use wait_for to wake up periodically and check if any effects have finished playing
            m_cv.wait_for(lock, std::chrono::milliseconds(100), [this] { 
                return !m_running || !m_tasks.empty(); 
            });

            if (!m_running && m_tasks.empty()) break;

            if (!m_tasks.empty()) {
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
        }

        if (task) {
            task();
        }

        // Check for finished effects (handles hardware status and software timeout fallback)
        PruneFinishedEffects();
    }
}

void HapticDevice::PruneFinishedEffects() {
    uint64_t now = SDL_GetTicks();
    bool hasStatusSupport = m_haptic && (SDL_GetHapticFeatures(m_haptic.Get()) & SDL_HAPTIC_STATUS);

    auto pruneMap = [this, now, hasStatusSupport](auto& activeMap, std::map<int, SDL_HapticEffectID>& ids) {
        std::vector<int> slotsToPrune;
        {
            std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
            for (auto const& [slot, info] : activeMap) {
                bool shouldPrune = false;

                // 1. Software timeout check (fallback for all devices, including gamepads)
                if (info.duration_ms != SDL_HAPTIC_INFINITY && info.duration_ms > 0) {
                    if (now > info.last_updated + info.duration_ms + 100) { // 100ms grace period
                        shouldPrune = true;
                    }
                }

                // 2. Hardware status check (if supported and we have a valid hardware effect ID)
                if (!shouldPrune && hasStatusSupport) {
                    auto idIt = ids.find(slot);
                    if (idIt != ids.end() && idIt->second != -1) {
                        if (!SDL_GetHapticEffectStatus(m_haptic.Get(), idIt->second)) {
                            shouldPrune = true;
                        }
                    }
                }

                if (shouldPrune) slotsToPrune.push_back(slot);
            }
        }

        for (int slot : slotsToPrune) {
            auto idIt = ids.find(slot);
            if (idIt != ids.end()) {
                if (idIt->second != -1 && m_haptic) SDL_DestroyHapticEffect(m_haptic.Get(), idIt->second);
                ids.erase(idIt);
            }
            std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
            activeMap.erase(slot);
        }
    };

    pruneMap(m_activeConstants,        m_constantEffects);
    pruneMap(m_activePeriodicEffects,  m_periodicEffects);
    pruneMap(m_activeRumbles,          m_rumbleEffects);
    pruneMap(m_activeConditions,       m_conditionEffects);
}

SDL_HapticEffectID HapticDevice::UploadEffect(const SDL_HapticEffect& effect, SDL_HapticEffectID existingId, bool* outCreated) {
    if (!m_haptic) return -1;

    if (existingId != -1) {
        if (SDL_UpdateHapticEffect(m_haptic.Get(), existingId, &effect)) {
            // Updated in-place — effect is already running, no restart needed.
            if (outCreated) *outCreated = false;
            return existingId;
        } else {
            LOG_ERROR("HapticDevice", "UploadEffect - Update failed for ID %d: %s. Recreating.", existingId, SDL_GetError());
        }
        SDL_DestroyHapticEffect(m_haptic.Get(), existingId);
    }

    SDL_HapticEffectID newId = SDL_CreateHapticEffect(m_haptic.Get(), &effect);
    if (newId == -1) {
        LOG_ERROR("HapticDevice", "UploadEffect - Create failed: %s", SDL_GetError());
    }
    if (outCreated) *outCreated = (newId != -1);
    return newId;
}

// --- Play Methods (base stubs — subclasses override for real hardware) ---

int HapticDevice::PlayConstant(int slot, float strength, uint32_t duration_ms) { return -1; }
int HapticDevice::PlayPeriodic(int slot, HapticPeriodicType wave_type, float strength, uint32_t period, float magnitude, float offset, uint32_t phase, uint32_t duration_ms) { return -1; }
int HapticDevice::PlayRumble(int slot, float large_magnitude, float small_magnitude, uint32_t duration_ms) { return -1; }
int HapticDevice::PlayCondition(int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, uint32_t duration_ms) { return -1; }
int HapticDevice::PlayDualSenseTrigger(const std::string& trigger, const std::string& effect_type, const std::map<std::string, int>& params) { return -1; }

// --- Stop Methods (base stubs) ---

int HapticDevice::StopConstant(int slot) { return -1; }
int HapticDevice::StopPeriodic(int slot) { return -1; }
int HapticDevice::StopRumble(int slot) { return -1; }
int HapticDevice::StopCondition(int slot) { return -1; }

// --- State Getters ---

std::map<int, ActiveConstantInfo> HapticDevice::GetActiveConstants() {
    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
    return m_activeConstants;
}

std::map<int, ActivePeriodicInfo> HapticDevice::GetActivePeriodicEffects() {
    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
    return m_activePeriodicEffects;
}

std::map<int, ActiveConditionInfo> HapticDevice::GetActiveConditions() {
    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
    return m_activeConditions;
}

std::map<int, ActiveRumbleInfo> HapticDevice::GetActiveRumbles() {
    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
    return m_activeRumbles;
}

std::map<std::string, ActiveDualSenseTriggerInfo> HapticDevice::GetActiveDualSenseTriggers() {
    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
    return m_activeDualSenseTriggers;
}

// --- Internal steering-wheel helpers (use kInternalSlot = -1 to avoid
//     colliding with any user-assigned slot) ---

void HapticDevice::SetConstantForce(float level, float direction) {
    RunAsync([this, level, direction]() {
        if (!m_haptic) {
            LOG_WARN("HapticDevice", "SetConstantForce - Haptic device not ready");
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_POLAR;
        effect.constant.direction.dir[0] = (Sint32)(direction * 100.0f);
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = (Sint16)(std::clamp(level, -1.0f, 1.0f) * 32767.0f);

        SDL_HapticEffectID existing = -1;
        auto it = m_constantEffects.find(kInternalSlot);
        if (it != m_constantEffects.end()) existing = it->second;

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_constantEffects[kInternalSlot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR("HapticDevice", "SetConstantForce - Run failed: %s", SDL_GetError());
                }
            }
        }
    });
}

void HapticDevice::SetPeriodic(HapticPeriodicType type, float magnitude, int period, float direction) {
    RunAsync([this, type, magnitude, period, direction]() {
        if (!m_haptic) return;

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));

        // Square wave: SDL3 has no native SQUARE type; synthesise it as a
        // SDL_HAPTIC_CUSTOM effect.  SetPeriodic has no offset parameter, so
        // the wave is centred at 0 (hi = +magnitude, lo = -magnitude).
        if (type == HapticPeriodicType::Square) {
            const auto hi_val = std::clamp( magnitude, -1.0f, 1.0f);
            const auto lo_val = std::clamp(-magnitude, -1.0f, 1.0f);
            Uint16 wave_data[2] = {
                static_cast<Uint16>(static_cast<Sint16>(hi_val * 32767.0f)),
                static_cast<Uint16>(static_cast<Sint16>(lo_val * 32767.0f))
            };
            const Uint16 half_period = static_cast<Uint16>(std::max(1, period / 2));

            effect.type = SDL_HAPTIC_CUSTOM;
            effect.custom.direction.type   = SDL_HAPTIC_POLAR;
            effect.custom.direction.dir[0] = (Sint32)(direction * 100.0f);
            effect.custom.channels         = 1;
            effect.custom.period           = half_period;
            effect.custom.samples          = 2;
            effect.custom.data             = wave_data;  // copied by SDL
            effect.custom.length           = SDL_HAPTIC_INFINITY;
        } else {
            effect.type = ToSDLPeriodicType(type);  // translate once, here
            effect.periodic.direction.type = SDL_HAPTIC_POLAR;
            effect.periodic.direction.dir[0] = (Sint32)(direction * 100.0f);
            effect.periodic.length = SDL_HAPTIC_INFINITY;
            effect.periodic.period = (Uint16)period;
            effect.periodic.magnitude = (Sint16)(std::clamp(magnitude, 0.0f, 1.0f) * 32767.0f);
        }

        SDL_HapticEffectID existing = -1;
        auto it = m_periodicEffects.find(kInternalSlot);
        if (it != m_periodicEffects.end()) existing = it->second;

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_periodicEffects[kInternalSlot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR("HapticDevice", "SetPeriodic - Run failed: %s", SDL_GetError());
                }
            }
        }
    });
}

void HapticDevice::SetCondition(HapticConditionType type, float saturation, float coefficient, float deadband, float center) {
    RunAsync([this, type, saturation, coefficient, deadband, center]() {
        if (!m_haptic) return;

        // Use a negative type-based key so these internal slots can never clash
        // with user-assigned condition slots (0, 1, 2, ...).
        const int internalKey = kInternalSlot - static_cast<int>(type);

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = ToSDLConditionType(type);  // translate once, here
        effect.condition.length = SDL_HAPTIC_INFINITY;

        Uint16 sat   = (Uint16)(std::clamp(saturation,   0.0f, 1.0f) * 0xFFFF);
        Sint16 coeff = (Sint16)(std::clamp(coefficient,  0.0f, 1.0f) * 32767.0f);
        Uint16 db    = (Uint16)(std::clamp(deadband,     0.0f, 1.0f) * 0xFFFF);
        Sint16 ctr   = (Sint16)(std::clamp(center,      -1.0f, 1.0f) * 32767.0f);

        effect.condition.right_sat[0]   = sat;
        effect.condition.left_sat[0]    = sat;
        effect.condition.right_coeff[0] = coeff;
        effect.condition.left_coeff[0]  = coeff;
        effect.condition.deadband[0]    = db;
        effect.condition.center[0]      = ctr;

        SDL_HapticEffectID existing = -1;
        auto it = m_conditionEffects.find(internalKey);
        if (it != m_conditionEffects.end()) existing = it->second;

        if (existing == -1) {
            LOG_WARN("HapticDevice", "SetCondition - existingID -1");
        } else {
            LOG_INFO("HapticDevice", "SetCondition - existingID %d", existing);
        }

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_conditionEffects[internalKey] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR("HapticDevice", "SetCondition - Run failed: %s", SDL_GetError());
                }
            }
        }
    });
}

void HapticDevice::SetRumble(float low_freq, float high_freq, Uint32 duration) {
    RunAsync([this, low_freq, high_freq, duration]() {
        if (!m_haptic) return;

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = duration;
        effect.leftright.large_magnitude = (Uint16)(std::clamp(low_freq,  0.0f, 1.0f) * 0xFFFF);
        effect.leftright.small_magnitude = (Uint16)(std::clamp(high_freq, 0.0f, 1.0f) * 0xFFFF);

        SDL_HapticEffectID existing = -1;
        auto it = m_rumbleEffects.find(kInternalSlot);
        if (it != m_rumbleEffects.end()) existing = it->second;

        bool created = false;
        SDL_HapticEffectID newId = UploadEffect(effect, existing, &created);
        if (newId != -1) {
            m_rumbleEffects[kInternalSlot] = newId;
            if (created) {
                if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                    LOG_ERROR("HapticDevice", "SetRumble - Run failed: %s", SDL_GetError());
                }
            }
        }
    });
}

void HapticDevice::UpdateEffect(SDL_HapticEffectID effectId, const SDL_HapticEffect& effect) {
    RunAsync([this, effectId, effect]() {
        if (!m_haptic) return;
        SDL_UpdateHapticEffect(m_haptic.Get(), effectId, &effect);
    });
}

void HapticDevice::StopAll() {
    RunAsync([this]() {
        if (!m_haptic) return;

        {
            std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
            m_activeConstants.clear();
            m_activePeriodicEffects.clear();
            m_activeConditions.clear();
            m_activeRumbles.clear();
            m_activeDualSenseTriggers.clear();
        }

        SDL_StopHapticEffects(m_haptic.Get());

        // Destroy every cached effect ID so the next Play* call creates a fresh
        // effect (some drivers refuse to re-run an INFINITY effect after Stop).
        auto destroyAll = [this](std::map<int, SDL_HapticEffectID>& effects) {
            for (auto& [slot, id] : effects) {
                if (id != -1) {
                    SDL_DestroyHapticEffect(m_haptic.Get(), id);
                    id = -1;
                }
            }
            effects.clear();
        };

        destroyAll(m_constantEffects);
        destroyAll(m_periodicEffects);
        destroyAll(m_rumbleEffects);
        destroyAll(m_conditionEffects);
    });
}