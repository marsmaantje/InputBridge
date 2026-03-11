#include "HapticDevice.h"
#include <algorithm>

HapticDevice::HapticDevice(SDL_Joystick* joystick) : m_joystick(joystick) {}

HapticDevice::~HapticDevice() {
    Close();
}

InputBridge::Result<bool, InputBridge::HapticError> HapticDevice::Init() {
    if (!m_joystick) {
        SDL_Log("HapticDevice::Init - Failed: Device not found (joystick is null)");
        return InputBridge::Result<bool, InputBridge::HapticError>::Err(InputBridge::HapticError::DeviceNotFound);
    }

    auto joystickID = SDL_GetJoystickID(m_joystick);
    bool isHaptic = SDL_IsJoystickHaptic(m_joystick);
    SDL_Log("HapticDevice::Init - Joystick ID %u, IsHaptic: %d", joystickID, isHaptic);

    if (isHaptic) {
        m_haptic.Reset(SDL_OpenHapticFromJoystick(m_joystick));
        if (!m_haptic) {
            SDL_Log("Warning: SDL_OpenHapticFromJoystick failed: %s", SDL_GetError());
            SDL_Log("Will continue - gamepad rumble fallback may still work, but advanced haptics will NOT work.");
            // FIXED: Don't return error here - allow fallback to gamepad rumble
            // Some controllers (like DualSense) work better with SDL_RumbleGamepad
        } else {
            if (SDL_HapticRumbleSupported(m_haptic.Get())) {
                if (!SDL_InitHapticRumble(m_haptic.Get())) {
                    SDL_Log("Warning: SDL_InitHapticRumble failed: %s", SDL_GetError());
                }
            }
            SDL_SetHapticGain(m_haptic.Get(), 100);
            SDL_SetHapticAutocenter(m_haptic.Get(), 0);
        }
    }

    // Start the async thread for both SDL_Haptic devices (steering wheels) AND gamepads.
    // Gamepads use SDL_RumbleGamepad but it still needs to be queued on the haptics thread
    // to maintain thread safety and consistency with the async architecture.
    if (m_haptic || SDL_IsGamepad(joystickID)) {
        m_running = true;
        m_thread = std::thread(&HapticDevice::ThreadLoop, this);
    }

    if (m_haptic || SDL_IsGamepad(joystickID)) {
        SDL_Log("HapticDevice::Init - Success");
        return InputBridge::Result<bool, InputBridge::HapticError>::Ok(true);
    }

    SDL_Log("HapticDevice::Init - Failed: Unsupported device type");
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

    // Thread is now joined; safe to call SDL haptic API directly.
    // StopAll() uses RunAsync and would silently drop after m_running=false,
    // so we call SDL_StopHapticEffects directly here.
    if (m_haptic) {
        SDL_StopHapticEffects(m_haptic.Get());
    }
    m_haptic.Reset();  // SDL_CloseHaptic cleans up all remaining effect slots
    m_constantEffectId = -1;
    m_periodicEffectId = -1;
    m_rumbleEffectId = -1;
    m_conditionEffects.clear();
}

void HapticDevice::RunAsync(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Do not accept new tasks after shutdown has been initiated.
        // This prevents tasks from accumulating in the queue after Close() and
        // avoids potential use-after-free if callers post tasks concurrently with
        // or after destruction.
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
            m_cv.wait(lock, [this] { return !m_running || !m_tasks.empty(); });
            if (!m_running && m_tasks.empty()) break;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        if (task) task();
    }
}

SDL_HapticEffectID HapticDevice::UploadEffect(const SDL_HapticEffect& effect, SDL_HapticEffectID existingId) {
    if (!m_haptic) return -1;

    if (existingId != -1) {
        if (SDL_UpdateHapticEffect(m_haptic.Get(), existingId, &effect)) {
            return existingId;
        } else {
            // If update fails (e.g. type mismatch), destroy and recreate
            SDL_Log("HapticDevice::UploadEffect - Update failed for ID %d: %s. Recreating.", existingId, SDL_GetError());
            SDL_DestroyHapticEffect(m_haptic.Get(), existingId);
        }
    }

    SDL_HapticEffectID newId = SDL_CreateHapticEffect(m_haptic.Get(), &effect);
    if (newId == -1) {
        SDL_Log("HapticDevice::UploadEffect - Create failed: %s", SDL_GetError());
    }
    return newId;
}

void HapticDevice::SetConstantForce(float level, float direction) {
    RunAsync([this, level, direction]() {
        if (!m_haptic) {
            SDL_Log("HapticDevice::SetConstantForce - Haptic device not ready");
            return;
        }

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_POLAR;
        effect.constant.direction.dir[0] = (Sint32)(direction * 100.0f);
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = (Sint16)(std::clamp(level, -1.0f, 1.0f) * 32767.0f);

        m_constantEffectId = UploadEffect(effect, m_constantEffectId);
        if (m_constantEffectId != -1) {
            if (!SDL_RunHapticEffect(m_haptic.Get(), m_constantEffectId, 1)) {
                SDL_Log("HapticDevice::SetConstantForce - Run failed: %s", SDL_GetError());
            }
        }
    });
}

void HapticDevice::SetPeriodic(Uint16 type, float magnitude, int period, float direction) {
    RunAsync([this, type, magnitude, period, direction]() {
        if (!m_haptic) return;

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = type;
        effect.periodic.direction.type = SDL_HAPTIC_POLAR;
        effect.periodic.direction.dir[0] = (Sint32)(direction * 100.0f);
        effect.periodic.length = SDL_HAPTIC_INFINITY;
        effect.periodic.period = (Uint16)period;
        effect.periodic.magnitude = (Sint16)(std::clamp(magnitude, 0.0f, 1.0f) * 32767.0f);

        m_periodicEffectId = UploadEffect(effect, m_periodicEffectId);
        if (m_periodicEffectId != -1) {
            if (!SDL_RunHapticEffect(m_haptic.Get(), m_periodicEffectId, 1)) {
                SDL_Log("HapticDevice::SetPeriodic - Run failed: %s", SDL_GetError());
            }
        }
    });
}

void HapticDevice::SetCondition(Uint16 type, float saturation, float coefficient, float deadband, float center) {
    RunAsync([this, type, saturation, coefficient, deadband, center]() {
        if (!m_haptic) {
            return;
        }

        const int slot = -1 - type; // Use negative, type-based slots to not conflict with user-defined slots
        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(SDL_HapticEffect));
        effect.type = type;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        
        Uint16 sat = (Uint16)(std::clamp(saturation, 0.0f, 1.0f) * 0xFFFF);
        Sint16 coeff = (Sint16)(std::clamp(coefficient, 0.0f, 1.0f) * 32767.0f);
        Uint16 db = (Uint16)(std::clamp(deadband, 0.0f, 1.0f) * 0xFFFF);
        Sint16 ctr = (Sint16)(std::clamp(center, -1.0f, 1.0f) * 32767.0f);

        effect.condition.right_sat[0] = sat;
        effect.condition.left_sat[0] = sat;
        effect.condition.right_coeff[0] = coeff;
        effect.condition.left_coeff[0] = coeff;
        effect.condition.deadband[0] = db;
        effect.condition.center[0] = ctr;

        SDL_HapticEffectID existingId = -1;
        if (m_conditionEffects.count(slot)) {
            existingId = m_conditionEffects[slot];
        }

        SDL_HapticEffectID newId = UploadEffect(effect, existingId);
        if (newId != -1) {
            m_conditionEffects[slot] = newId;
            if (!SDL_RunHapticEffect(m_haptic.Get(), newId, 1)) {
                SDL_Log("HapticDevice::SetCondition - Run failed: %s", SDL_GetError());
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
        effect.leftright.large_magnitude = (Uint16)(std::clamp(low_freq, 0.0f, 1.0f) * 0xFFFF);
        effect.leftright.small_magnitude = (Uint16)(std::clamp(high_freq, 0.0f, 1.0f) * 0xFFFF);

        m_rumbleEffectId = UploadEffect(effect, m_rumbleEffectId);
        if (m_rumbleEffectId != -1) {
            if (!SDL_RunHapticEffect(m_haptic.Get(), m_rumbleEffectId, 1)) {
                SDL_Log("HapticDevice::SetRumble - Run failed: %s", SDL_GetError());
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
    // Queue the stop on the haptic thread so that it doesn't race with any
    // in-flight effect operations already dispatched via RunAsync.
    RunAsync([this]() {
        if (!m_haptic) return;

        SDL_StopHapticEffects(m_haptic.Get());

        // Destroy and invalidate every cached effect slot.
        // This is the key fix for the spring/condition update bug:
        //   After SDL_StopHapticEffects, some drivers refuse to update or
        //   re-run an effect that was previously set to SDL_HAPTIC_INFINITY.
        //   By destroying the effect IDs here, the next SetCondition /
        //   SetConstantForce / SetPeriodic call will create a fresh effect,
        //   guaranteeing it starts playing regardless of prior state.
        auto destroyEffect = [this](SDL_HapticEffectID& id) {
            if (id != -1) {
                SDL_DestroyHapticEffect(m_haptic.Get(), id);
                id = -1;
            }
        };

        destroyEffect(m_constantEffectId);
        destroyEffect(m_periodicEffectId);
        destroyEffect(m_rumbleEffectId);

        for (auto& [type, id] : m_conditionEffects) {
            if (id != -1) {
                SDL_DestroyHapticEffect(m_haptic.Get(), id);
                id = -1;
            }
        }
        m_conditionEffects.clear();
    });
}