#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>
#include "Utils/Result.h"
#include "Utils/UniqueHandle.h"

namespace InputBridge {
    enum class HapticError {
        DeviceNotFound,
        UnsupportedEffect,
        InitFailed
    };
}

class HapticDevice {
public:
    HapticDevice(SDL_Joystick* joystick);
    virtual ~HapticDevice();

    virtual InputBridge::Result<bool, InputBridge::HapticError> Init();
    virtual bool IsReady() const;
    void Close();
    virtual void StopAll();

    void SetConstantForce(float level, float direction);
    void SetPeriodic(Uint16 type, float magnitude, int period, float direction);
    void SetCondition(Uint16 type, float saturation, float coefficient, float deadband, float center);
    void SetRumble(float low_freq, float high_freq, Uint32 duration);
    void UpdateEffect(SDL_HapticEffectID effectId, const SDL_HapticEffect& effect);

protected:
    void RunAsync(std::function<void()> task);
    SDL_HapticEffectID UploadEffect(const SDL_HapticEffect& effect, SDL_HapticEffectID existingId);

    SDL_Joystick* m_joystick;
    UniqueHandle<SDL_Haptic> m_haptic;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_tasks;
    bool m_running = false;

    SDL_HapticEffectID m_constantEffectId = -1;
    SDL_HapticEffectID m_periodicEffectId = -1;
    SDL_HapticEffectID m_rumbleEffectId = -1;
    std::map<int, SDL_HapticEffectID> m_conditionEffects; // Key: user-defined slot, Value: effect ID

private:
    void ThreadLoop();
};