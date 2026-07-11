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
// • Provides Xbox impulse trigger effect support.
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
#include <cstdint>
#include <SDL3/SDL.h>
#include "Devices/DeviceManager.h"

struct HapticTarget;
class PreferencesManager;

struct HapticCommand {
    enum Type { RUMBLE, CONSTANT, PERIODIC, CONDITION, GAIN, DUALSENSE_TRIGGER, XBOX_TRIGGER } type;
    int virtual_id;
    float fParams[8]; // Generic float storage
    int iParams[10];  // Generic int storage - increased for more params
    char sParams[2][32]; // String params: [0]=trigger ("left"/"right"/"both"), [1]=effect_type
};

// DualSense adaptive trigger effects that carry a 10-element per-position array
// (MultiplePositionFeedback/MultiplePositionVibration) don't fit HapticCommand's
// fixed 10-slot iParams - those are already fully used by the scalar-only
// DualSense effects (feedback/weapon/vibration/slope_feedback/bow/galloping/
// machine, see QueueDualSenseTrigger). Rather than growing iParams (which would
// affect every other effect type's marshaling), these two get their own small,
// dedicated queue.
struct DualSenseArrayCommand {
    enum Type { MULTI_POSITION_FEEDBACK, MULTI_POSITION_VIBRATION } type;
    int virtual_id;
    char trigger[8];     // "left" / "right" / "both"
    uint8_t frequency;   // only used by MULTI_POSITION_VIBRATION
    uint8_t values[10];  // per-position strength (feedback) or amplitude (vibration)
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
    // Array-based DualSense effects - see DualSenseArrayCommand above for why
    // these bypass QueueDualSenseTrigger/HapticCommand.
    void QueueDualSenseMultiPositionFeedback(int virtual_id, const char* trigger, const uint8_t strengths[10]);
    void QueueDualSenseMultiPositionVibration(int virtual_id, const char* trigger, uint8_t frequency, const uint8_t amplitudes[10]);
    void QueueXboxTrigger(int virtual_id, int left_intensity, int right_intensity, int duration_ms);

private:
    OutputMapper(const DeviceManager& deviceManager);

    static std::unique_ptr<OutputMapper> s_Instance;

    const DeviceManager& m_DeviceManager;

    std::vector<HapticTarget>* m_active_targets = nullptr;

    std::mutex m_Mutex;
    std::vector<HapticCommand> m_CommandQueue;

    std::mutex m_ArrayMutex;
    std::vector<DualSenseArrayCommand> m_ArrayCommandQueue;

    std::atomic<uint64_t> m_lastHapticActivityTime{0};

    void QueueCommand(HapticCommand&& cmd);
    void QueueArrayCommand(DualSenseArrayCommand&& cmd);
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
    void TriggerDualSenseMultiPositionFeedback(int virtual_id, const char* trigger, const uint8_t* strengths);
    void TriggerDualSenseMultiPositionVibration(int virtual_id, const char* trigger, uint8_t frequency, const uint8_t* amplitudes);
    void TriggerXboxTrigger(int virtual_id, int left_intensity, int right_intensity, int duration_ms);
};