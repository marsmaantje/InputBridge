#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// OutputMapper provides the central interface for output-device haptics.
//
// Features:
// • Manages active haptic output targets and device lifecycle events.
// • Processes and dispatches queued haptic commands from external threads.
// • Supports multiple force-feedback effect types, including rumble,
//   constant force, periodic effects, condition effects, and gain control.
// • Provides DualSense adaptive trigger effect support.
// • Exposes a thread-safe API for servers and background systems to
//   generate haptic feedback.
// • Tracks haptic activity state and offers runtime controls for
//   stopping effects and monitoring output status.
//
// All haptic effects are queued from external callers and executed on
// the main thread to ensure safe interaction with underlying devices.
// ─────────────────────────────────────────────────────────────────────────────

#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <SDL3/SDL.h>
#include "Devices/DeviceManager.h"

struct HapticTarget;
class PreferencesManager;

struct HapticCommand {
    enum Type { RUMBLE, CONSTANT, PERIODIC, CONDITION, GAIN, DUALSENSE_TRIGGER } type;
    int virtual_id;
    float fParams[8]; // Generic float storage
    int iParams[10];  // Generic int storage - increased for more params
    char sParams[2][32]; // String params: [0]=trigger ("left"/"right"/"both"), [1]=effect_type
};

class OutputMapper {
public:
    static OutputMapper& GetInstance();
    static void Init(const DeviceManager& deviceManager);
    static void Shutdown();

    ~OutputMapper();

    OutputMapper(const OutputMapper&) = delete;
    OutputMapper& operator=(const OutputMapper&) = delete;
    void DrawContent();
    void DrawContentOnly(); // Draws content without Begin/End

    // Call this every frame in the main loop to process queued haptic commands
    void Update();

    void SetActiveHapticTargets(std::vector<HapticTarget>* targets);

    void HandleDeviceConnectionChange();

    void StopAllHapticEffects();
    bool IsHapticsActive() const;

    // Thread-safe API for external servers
    void QueueRumble(int virtual_id, int slot, float low_freq, float high_freq, int duration_ms);
    void QueueConstantForce(int virtual_id, int slot, float strength, int duration_ms);
    void QueuePeriodic(int virtual_id, int slot, HapticPeriodicType wave_type, float strength, int period, float magnitude, float offset, int phase, int duration_ms);
    void QueueCondition(int virtual_id, int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms);
    void QueueSetGain(int virtual_id, int gain);
    void QueueDualSenseTrigger(int virtual_id, const char* trigger, const char* effect_type,
                               int position, int strength, int end_position,
                               int amplitude, int frequency, int snap_force,
                               int first_foot, int second_foot, int period,
                               int amplitude_a, int amplitude_b);

private:
    OutputMapper(const DeviceManager& deviceManager);

    static std::unique_ptr<OutputMapper> s_Instance;

    const DeviceManager& m_DeviceManager;

    std::vector<HapticTarget>* m_active_targets = nullptr;

    std::mutex m_Mutex;
    std::vector<HapticCommand> m_CommandQueue;

    std::atomic<uint64_t> m_lastHapticActivityTime{0};

    void QueueCommand(HapticCommand&& cmd);
    void GetTargets(int virtual_id, std::vector<HapticTarget*>& out_targets);
    void UpdateHapticDevice(HapticTarget& target);
    void CloseHapticDevice(HapticTarget& target);

    // Internal triggers (Main Thread Only)
    void TriggerRumble(int virtual_id, int slot, float low_freq, float high_freq, int duration_ms);
    void TriggerConstantForce(int virtual_id, int slot, float strength, int duration_ms);
    void TriggerPeriodic(int virtual_id, int slot, HapticPeriodicType wave_type, float strength, int period, float magnitude, float offset, int phase, int duration_ms);
    void TriggerCondition(int virtual_id, int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms);
    void TriggerSetGain(int virtual_id, int gain);
    void TriggerDualSenseTrigger(int virtual_id, const char* trigger, const char* effect_type,
                                 int position, int strength, int end_position,
                                 int amplitude, int frequency, int snap_force,
                                 int first_foot, int second_foot, int period,
                                 int amplitude_a, int amplitude_b);
};