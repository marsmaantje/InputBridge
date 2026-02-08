#pragma once

#include <cstdint>

struct DualSenseTriggerEffect {
    uint8_t Mode;
    uint8_t Params[10];
};

class DualSenseTriggerEffectGenerator {
public:
    static DualSenseTriggerEffect Off();
    static DualSenseTriggerEffect Feedback(uint8_t startPosition, uint8_t strength);
    static DualSenseTriggerEffect Weapon(uint8_t startPosition, uint8_t endPosition, uint8_t strength);
    static DualSenseTriggerEffect Vibration(uint8_t position, uint8_t amplitude, uint8_t frequency);
    static DualSenseTriggerEffect Bow(uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t snapForce);
    static DualSenseTriggerEffect Galloping(uint8_t startPosition, uint8_t endPosition, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency);
    static DualSenseTriggerEffect MachineGun(uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t frequency);
    
    // Helper to apply an effect to a specific trigger in the output report buffer
    static void ApplyToBuffer(uint8_t* buffer, std::size_t offset, const DualSenseTriggerEffect& effect);
};