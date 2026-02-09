#pragma once

#include <cstddef> // for std::size_t
#include <cstdint> // for uint8_t
#include <vector>
#include <array>

struct DualSenseTriggerEffect {
    uint8_t Mode{0};
    std::array<uint8_t, 10> Params{};
};

class DualSenseTriggerEffectGenerator {
  public:
    enum class TriggerEffect : uint8_t {
        Off = 0x05,
        // Simple
        Rigid = 0x01,
        Rigid_A = 0x01,
        Rigid_B = 0x01,
        Rigid_AB = 0x01,
        // Pulse
        Pulse = 0x02,
        Pulse_A = 0x02,
        Pulse_B = 0x02,
        Pulse_AB = 0x02,
        // Official
        Feedback = 0x21,
        Weapon = 0x25,
        Bow = 0x22,
        Galloping = 0x23,
        SemiAutomatic = 0x24,
        Automatic = 0x27,
        Machine = 0x26,
    };

    DualSenseTriggerEffectGenerator() = delete;

    static DualSenseTriggerEffect Off();
    static DualSenseTriggerEffect Feedback(uint8_t startPosition, uint8_t strength);
    static DualSenseTriggerEffect Weapon(uint8_t startPosition, uint8_t endPosition, uint8_t strength);
    static DualSenseTriggerEffect Vibration(uint8_t position, uint8_t amplitude, uint8_t frequency);
    static DualSenseTriggerEffect Bow(uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t snapForce);
    static DualSenseTriggerEffect Galloping(uint8_t startPosition, uint8_t endPosition, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency);
    static DualSenseTriggerEffect MachineGun(uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t frequency);

    // Helper to apply an effect to a specific trigger in the output report buffer
    static void ApplyToBuffer(uint8_t *buffer, std::size_t offset, const DualSenseTriggerEffect &effect);

    static DualSenseTriggerEffect CreateEffect(TriggerEffect effect, const std::vector<uint8_t> &parameters);
    static DualSenseTriggerEffect CreateEffect(uint8_t effect, const std::vector<uint8_t> &parameters);

    static DualSenseTriggerEffect CreateOff();
    static DualSenseTriggerEffect CreateRigid(uint8_t start, uint8_t force);
    static DualSenseTriggerEffect CreateRigid_A(uint8_t start, uint8_t force);
    static DualSenseTriggerEffect CreateRigid_B(uint8_t start, uint8_t force);
    static DualSenseTriggerEffect CreateRigid_AB(uint8_t start, uint8_t force);
    static DualSenseTriggerEffect CreatePulse(uint8_t start, uint8_t end, uint8_t force);
    static DualSenseTriggerEffect CreatePulse_A(uint8_t start, uint8_t end, uint8_t force);
    static DualSenseTriggerEffect CreatePulse_B(uint8_t start, uint8_t end, uint8_t force);
    static DualSenseTriggerEffect CreatePulse_AB(uint8_t start, uint8_t end, uint8_t force);
    static DualSenseTriggerEffect CreateFeedback(uint8_t position, uint8_t strength);
    static DualSenseTriggerEffect CreateWeapon(uint8_t start, uint8_t end, uint8_t strength);
    static DualSenseTriggerEffect CreateBow(uint8_t start, uint8_t end, uint8_t strength, uint8_t snapForce);
    static DualSenseTriggerEffect CreateGalloping(uint8_t start, uint8_t end, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency);
    static DualSenseTriggerEffect CreateSemiAutomatic(uint8_t start, uint8_t end, uint8_t strength);
    static DualSenseTriggerEffect CreateAutomatic(uint8_t start, uint8_t end, uint8_t strength, uint8_t period);
    static DualSenseTriggerEffect CreateMachine(uint8_t start, uint8_t strength, uint8_t frequency, uint8_t period);
};
