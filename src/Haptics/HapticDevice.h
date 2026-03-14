#pragma once

#include <SDL3/SDL.h>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include "Utils/SDLHandles.h"
#include "Core/Result.h"

// --- Active Effect Info Structs ---
struct ActiveConstantInfo {
    bool active = false;
    float strength = 0.0f;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
};

struct ActivePeriodicInfo {
    bool active = false;
    float strength = 0.0f;
    uint32_t period = 0;
    float magnitude = 0.0f;
    float offset = 0.0f;
    uint32_t phase = 0;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
};

struct ActiveConditionInfo {
    uint16_t type = 0;
    float right_sat = 0.0f;
    float left_sat = 0.0f;
    float right_coeff = 0.0f;
    float left_coeff = 0.0f;
    float deadband = 0.0f;
    float center = 0.0f;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
};

struct ActiveRumbleInfo {
    bool active = false;
    float large_magnitude = 0.0f;
    float small_magnitude = 0.0f;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
};

struct ActiveDualSenseTriggerInfo {
    std::string effect_type;
    std::map<std::string, int> params;
    uint64_t last_updated = 0;
};

class HapticDevice {
public:
    // Slot reserved for the internal SetConstantForce / SetPeriodic / SetRumble
    // helpers so they never collide with user-assigned slots (0, 1, 2, ...).
    static constexpr int kInternalSlot = -1;

    HapticDevice(SDL_Joystick* joystick);
    virtual ~HapticDevice();

    InputBridge::Result<bool, InputBridge::HapticError> Init();
    void Close();
    virtual bool IsReady() const;
    SDL_Haptic* GetHandle() const { return m_haptic.Get(); }

    // --- Play Methods ---
    // All Play* methods accept a slot so multiple independent instances of the
    // same effect type can run simultaneously, mirroring PlayCondition's design.
    virtual int PlayConstant(int slot, float strength, uint32_t duration_ms);
    virtual int PlayPeriodic(int slot, float strength, uint32_t period, float magnitude, float offset, uint32_t phase, uint32_t duration_ms);
    virtual int PlayRumble(int slot, float large_magnitude, float small_magnitude, uint32_t duration_ms);
    virtual int PlayCondition(int slot, uint16_t type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, uint32_t duration_ms);

    // --- Stop Methods ---
    virtual int StopConstant(int slot);
    virtual int StopPeriodic(int slot);
    virtual int StopRumble(int slot);
    virtual int StopCondition(int slot);

    virtual int PlayDualSenseTrigger(const std::string& trigger, const std::string& effect_type, const std::map<std::string, int>& params);

    // --- State Getters (per-slot maps) ---
    virtual std::map<int, ActiveConstantInfo>  GetActiveConstants();
    virtual std::map<int, ActivePeriodicInfo>  GetActivePeriodicEffects();
    virtual std::map<int, ActiveConditionInfo> GetActiveConditions();
    virtual std::map<int, ActiveRumbleInfo>    GetActiveRumbles();
    virtual std::map<std::string, ActiveDualSenseTriggerInfo> GetActiveDualSenseTriggers();

    // Steering Wheel Effects
    // level: -1.0 to 1.0
    // direction: 0.0 to 360.0 (degrees)
    void SetConstantForce(float level, float direction = 0.0f);

    // type: SDL_HAPTIC_SINE, SDL_HAPTIC_TRIANGLE, etc.
    // magnitude: 0.0 to 1.0
    // period: milliseconds
    // direction: 0.0 to 360.0 (degrees)
    void SetPeriodic(Uint16 type, float magnitude, int period, float direction = 0.0f);

    // type: SDL_HAPTIC_SPRING, SDL_HAPTIC_DAMPER, etc.
    // saturation: 0.0 to 1.0
    // coefficient: 0.0 to 1.0
    // deadband: 0.0 to 1.0
    // center: -1.0 to 1.0
    void SetCondition(Uint16 type, float saturation, float coefficient, float deadband, float center);

    // Gamepad Effects
    // low_freq, high_freq: 0.0 to 1.0
    // duration: milliseconds
    void SetRumble(float low_freq, float high_freq, Uint32 duration);

    void UpdateEffect(SDL_HapticEffectID effectId, const SDL_HapticEffect& effect);

    virtual void StopAll();

protected:
    SDL_Joystick* m_joystick = nullptr;  // Non-owning pointer
    InputBridge::HapticHandle m_haptic;  // RAII ownership of haptic device

    // State for active effects UI — all keyed by slot for uniformity.
    std::mutex m_activeEffectsMutex;
    std::map<int, ActiveConstantInfo>  m_activeConstants;
    std::map<int, ActivePeriodicInfo>  m_activePeriodicEffects;
    std::map<int, ActiveConditionInfo> m_activeConditions;
    std::map<int, ActiveRumbleInfo>    m_activeRumbles;
    std::map<std::string, ActiveDualSenseTriggerInfo> m_activeDualSenseTriggers; // "left", "right"

    // Slot -> SDL effect ID maps for all effect types.
    std::map<int, SDL_HapticEffectID> m_constantEffects;
    std::map<int, SDL_HapticEffectID> m_periodicEffects;
    std::map<int, SDL_HapticEffectID> m_rumbleEffects;
    std::map<int, SDL_HapticEffectID> m_conditionEffects;

    SDL_HapticEffectID UploadEffect(const SDL_HapticEffect& effect, SDL_HapticEffectID existingId);
    void RunAsync(std::function<void()> task);

private:
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_tasks;
    bool m_running = false;
    void ThreadLoop();
};
