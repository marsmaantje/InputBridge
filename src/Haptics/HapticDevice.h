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

// ─── Condition effect type ────────────────────────────────────────────────────
//
// User-facing index (0–3) that maps to the four SDL condition effect types.
// Using a clean 0-based enum everywhere avoids exposing SDL's internal bitmask
// values (128 / 256 / 512 / 1024) to users, network protocols, and UI code.
//
// The translation to SDL_HAPTIC_* happens in exactly one place:
//   ToSDLConditionType()  →  called inside PlayCondition() implementations.

enum class HapticConditionType : int {
    Spring   = 0,   // SDL_HAPTIC_SPRING   (restoring force toward centre)
    Damper   = 1,   // SDL_HAPTIC_DAMPER   (resistance proportional to velocity)
    Inertia  = 2,   // SDL_HAPTIC_INERTIA  (resistance proportional to acceleration)
    Friction = 3,   // SDL_HAPTIC_FRICTION (constant resistance)
};

// Convert a user-facing index to the SDL bitmask.  Returns SDL_HAPTIC_SPRING
// for any out-of-range value so callers always get a valid type.
inline Uint16 ToSDLConditionType(HapticConditionType t) {
    switch (t) {
        case HapticConditionType::Spring:   return SDL_HAPTIC_SPRING;
        case HapticConditionType::Damper:   return SDL_HAPTIC_DAMPER;
        case HapticConditionType::Inertia:  return SDL_HAPTIC_INERTIA;
        case HapticConditionType::Friction: return SDL_HAPTIC_FRICTION;
        default:                            return SDL_HAPTIC_SPRING;
    }
}

// Convert a raw integer (0–3) to HapticConditionType, clamping to Spring on
// out-of-range input.
inline HapticConditionType ConditionTypeFromIndex(int index) {
    if (index < 0 || index > 3) return HapticConditionType::Spring;
    return static_cast<HapticConditionType>(index);
}

// Convert an SDL bitmask back to the user-facing enum (for display).
inline HapticConditionType ConditionTypeFromSDL(Uint16 sdlType) {
    switch (sdlType) {
        case SDL_HAPTIC_SPRING:   return HapticConditionType::Spring;
        case SDL_HAPTIC_DAMPER:   return HapticConditionType::Damper;
        case SDL_HAPTIC_INERTIA:  return HapticConditionType::Inertia;
        case SDL_HAPTIC_FRICTION: return HapticConditionType::Friction;
        default:                  return HapticConditionType::Spring;
    }
}

inline const char* ConditionTypeName(HapticConditionType t) {
    switch (t) {
        case HapticConditionType::Spring:   return "Spring";
        case HapticConditionType::Damper:   return "Damper";
        case HapticConditionType::Inertia:  return "Inertia";
        case HapticConditionType::Friction: return "Friction";
        default:                            return "Spring";
    }
}

// ─── Periodic effect wave type ────────────────────────────────────────────────
//
// User-facing index (0–4) for the five supported periodic waveform types.
// The order matches the SDL3 SDL_HapticType enumeration:
//   SDL_HAPTIC_SINE / SDL_HAPTIC_SQUARE / SDL_HAPTIC_TRIANGLE /
//   SDL_HAPTIC_SAWTOOTHUP / SDL_HAPTIC_SAWTOOTHDOWN
//
// Square does not have a native SDL3 type (SDL_HAPTIC_SQUARE was removed); it
// is synthesised as a SDL_HAPTIC_CUSTOM effect with a 2-sample waveform inside
// PlayPeriodic() implementations.  ToSDLPeriodicType() must therefore NOT be
// called with Square; it returns SDL_HAPTIC_SINE as a safe fallback if it
// ever is.
//
// The translation to SDL_HAPTIC_* happens in exactly one place:
//   ToSDLPeriodicType()  →  called inside PlayPeriodic() implementations
//                           (for all types except Square).

enum class HapticPeriodicType : int {
    Sine         = 0,  // SDL_HAPTIC_SINE        — smooth sinusoidal wave
    Square       = 1,  // SDL_HAPTIC_CUSTOM (synthesised) — instant high/low
    Triangle     = 2,  // SDL_HAPTIC_TRIANGLE    — linear ramp up/down
    SawtoothUp   = 3,  // SDL_HAPTIC_SAWTOOTHUP  — fast rise, instant drop
    SawtoothDown = 4,  // SDL_HAPTIC_SAWTOOTHDOWN — instant rise, fast drop
};

inline Uint16 ToSDLPeriodicType(HapticPeriodicType t) {
    switch (t) {
        case HapticPeriodicType::Sine:         return SDL_HAPTIC_SINE;
        case HapticPeriodicType::Triangle:     return SDL_HAPTIC_TRIANGLE;
        case HapticPeriodicType::SawtoothUp:   return SDL_HAPTIC_SAWTOOTHUP;
        case HapticPeriodicType::SawtoothDown: return SDL_HAPTIC_SAWTOOTHDOWN;
        // Square is synthesised via SDL_HAPTIC_CUSTOM in PlayPeriodic().
        // This path should never be reached; return Sine as a safe fallback.
        case HapticPeriodicType::Square:
        default:                               return SDL_HAPTIC_SINE;
    }
}

inline HapticPeriodicType PeriodicTypeFromIndex(int index) {
    if (index < 0 || index > 4) return HapticPeriodicType::Sine;
    return static_cast<HapticPeriodicType>(index);
}

inline HapticPeriodicType PeriodicTypeFromSDL(Uint16 sdlType) {
    switch (sdlType) {
        case SDL_HAPTIC_SINE:         return HapticPeriodicType::Sine;
        case SDL_HAPTIC_TRIANGLE:     return HapticPeriodicType::Triangle;
        case SDL_HAPTIC_SAWTOOTHUP:   return HapticPeriodicType::SawtoothUp;
        case SDL_HAPTIC_SAWTOOTHDOWN: return HapticPeriodicType::SawtoothDown;
        // SDL_HAPTIC_CUSTOM has no single equivalent; default to Sine.
        default:                      return HapticPeriodicType::Sine;
    }
}

inline const char* PeriodicTypeName(HapticPeriodicType t) {
    switch (t) {
        case HapticPeriodicType::Sine:         return "Sine";
        case HapticPeriodicType::Square:       return "Square";
        case HapticPeriodicType::Triangle:     return "Triangle";
        case HapticPeriodicType::SawtoothUp:   return "Sawtooth Up";
        case HapticPeriodicType::SawtoothDown: return "Sawtooth Down";
        default:                               return "Sine";
    }
}

// ─── Active Effect Info Structs ───────────────────────────────────────────────
struct ActiveConstantInfo {
    bool active = false;
    float strength = 0.0f;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
};

struct ActivePeriodicInfo {
    bool active = false;
    HapticPeriodicType wave_type = HapticPeriodicType::Sine;
    float strength = 0.0f;
    uint32_t period = 0;
    float magnitude = 0.0f;
    float offset = 0.0f;
    uint32_t phase = 0;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
};

struct ActiveConditionInfo {
    HapticConditionType type = HapticConditionType::Spring;
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
    virtual int PlayPeriodic(int slot, HapticPeriodicType wave_type, float strength, uint32_t period, float magnitude, float offset, uint32_t phase, uint32_t duration_ms);
    virtual int PlayRumble(int slot, float large_magnitude, float small_magnitude, uint32_t duration_ms);
    virtual int PlayCondition(int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, uint32_t duration_ms);

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

    // type: HapticPeriodicType::Sine, Triangle, SawtoothUp or SawtoothDown
    // magnitude: 0.0 to 1.0
    // period: milliseconds
    // direction: 0.0 to 360.0 (degrees)
    void SetPeriodic(HapticPeriodicType type, float magnitude, int period, float direction = 0.0f);

    // type: HapticConditionType::Spring, Damper, Inertia or Friction
    // saturation: 0.0 to 1.0
    // coefficient: 0.0 to 1.0
    // deadband: 0.0 to 1.0
    // center: -1.0 to 1.0
    void SetCondition(HapticConditionType type, float saturation, float coefficient, float deadband, float center);

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

    // Upload (create or update) a haptic effect.
    // Returns the effect ID on success, or -1 on failure.
    // Sets *outCreated to true when the effect was newly created (needs SDL_RunHapticEffect),
    // or false when an existing effect was updated in-place (already running, no restart needed).
    SDL_HapticEffectID UploadEffect(const SDL_HapticEffect& effect, SDL_HapticEffectID existingId, bool* outCreated = nullptr);
    void RunAsync(std::function<void()> task);

private:
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_tasks;
    bool m_running = false;
    void ThreadLoop();
};