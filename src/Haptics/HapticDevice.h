#pragma once

#include <SDL3/SDL.h>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include "../Utils/SDLHandles.h"
#include "../Core/Result.h"

class HapticDevice {
public:
    HapticDevice(SDL_Joystick* joystick);
    virtual ~HapticDevice();

    InputBridge::Result<bool, InputBridge::HapticError> Init();
    void Close();
    virtual bool IsReady() const;
    SDL_Haptic* GetHandle() const { return m_haptic.Get(); }

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

    void StopAll();

protected:
    SDL_Joystick* m_joystick = nullptr;  // Non-owning pointer
    InputBridge::HapticHandle m_haptic;  // RAII ownership of haptic device
    
    SDL_HapticEffectID m_constantEffectId = -1;
    SDL_HapticEffectID m_periodicEffectId = -1;
    SDL_HapticEffectID m_rumbleEffectId = -1;
    std::map<Uint16, SDL_HapticEffectID> m_conditionEffects;

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
