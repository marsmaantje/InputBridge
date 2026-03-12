#pragma once
#include "Haptics/HapticDevice.h"
#include <map>
#include <mutex>

struct ActiveConstantInfo {
    float strength = 0.0f;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
    bool active = false;
};

struct ActivePeriodicInfo {
    float strength = 0.0f;
    uint32_t period = 0;
    float magnitude = 0.0f;
    float offset = 0.0f;
    uint32_t phase = 0;
    uint32_t duration_ms = 0;
    uint64_t last_updated = 0;
    bool active = false;
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

class SteeringWheelHaptics : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    int SetGain(int gain);
    int PlayConstant(float strength, uint32_t duration_ms);
    int PlayPeriodic(float strength, uint32_t period, float magnitude, float offset, uint32_t phase, uint32_t duration_ms);
    int PlayCondition(int slot, uint16_t type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, uint32_t duration_ms);
    int StopCondition(int slot);
    void StopAll() override;

    std::map<int, ActiveConditionInfo> GetActiveConditions();
    ActiveConstantInfo GetActiveConstant();
    ActivePeriodicInfo GetActivePeriodic();
private:
    std::map<int, ActiveConditionInfo> m_activeConditions;
    std::mutex m_activeConditionsMutex;

    ActiveConstantInfo  m_activeConstant;
    ActivePeriodicInfo  m_activePeriodic;
    std::mutex          m_activeSimpleMutex; // guards m_activeConstant and m_activePeriodic
};