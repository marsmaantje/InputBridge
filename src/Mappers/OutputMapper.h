#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <SDL3/SDL.h>
#include "Devices/DeviceManager.h"
#include "Preferences/Preferences.h"

struct HapticTarget {
    int virtual_id = 0;
    std::string name;
    std::string device_guid;
    SDL_JoystickID instance_id = 0;
    SDL_Haptic* haptic_device = nullptr;

    // Cached Effect IDs
    int constant_effect_id = -1;
    int periodic_effect_id = -1;
    int condition_effect_id = -1;
    int rumble_effect_id = -1;
};

struct HapticCommand {
    enum Type { RUMBLE, CONSTANT, PERIODIC, CONDITION } type;
    int virtual_id;
    float fParams[8]; // Generic float storage
    int iParams[4];   // Generic int storage
};

class OutputMapper {
public:
    static OutputMapper& GetInstance();
    static void Init(const DeviceManager& deviceManager);
    static void Shutdown();

    ~OutputMapper();

    OutputMapper(const OutputMapper&) = delete;
    OutputMapper& operator=(const OutputMapper&) = delete;
    void LoadConfig(PreferencesManager& prefs);
    void SaveConfig() const;
    void DrawContent();

    // Call this every frame in the main loop to process queued haptic commands
    void Update();

    void HandleDeviceConnectionChange();

    // Thread-safe API for external servers
    void QueueRumble(int virtual_id, float low_freq, float high_freq, int duration_ms);
    void QueueConstantForce(int virtual_id, float strength, int duration_ms);
    void QueuePeriodic(int virtual_id, float strength, int period, float magnitude, float offset, int phase, int duration_ms);
    void QueueCondition(int virtual_id, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms);

private:
    OutputMapper(const DeviceManager& deviceManager);

    static std::unique_ptr<OutputMapper> s_Instance;

    const DeviceManager& m_DeviceManager;
    std::vector<HapticTarget> m_Targets;

    std::mutex m_Mutex;
    std::vector<HapticCommand> m_CommandQueue;

    HapticTarget* GetTarget(int virtual_id);
    void UpdateHapticDevice(HapticTarget& target);
    void CloseHapticDevice(HapticTarget& target);

    // Internal triggers (Main Thread Only)
    void TriggerRumble(int virtual_id, float low_freq, float high_freq, int duration_ms);
    void TriggerConstantForce(int virtual_id, float strength, int duration_ms);
    void TriggerPeriodic(int virtual_id, float strength, int period, float magnitude, float offset, int phase, int duration_ms);
    void TriggerCondition(int virtual_id, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms);
};
