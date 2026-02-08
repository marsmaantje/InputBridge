#include "DualSenseTriggerEffectGenerator.h"
#include <cstring>

void DualSenseTriggerEffectGenerator::ApplyToBuffer(uint8_t* buffer, size_t offset, const DualSenseTriggerEffect& effect) {
    buffer[offset] = effect.Mode;
    std::memcpy(buffer + offset + 1, effect.Params, sizeof(effect.Params));
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Off() {
    DualSenseTriggerEffect effect = {0};
    effect.Mode = 0x00;
    return effect;
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Feedback(uint8_t startPosition, uint8_t strength) {
    DualSenseTriggerEffect effect = {0};
    effect.Mode = 0x01;
    effect.Params[0] = startPosition;
    effect.Params[1] = strength;
    return effect;
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Weapon(uint8_t startPosition, uint8_t endPosition, uint8_t strength) {
    DualSenseTriggerEffect effect = {0};
    effect.Mode = 0x02;
    effect.Params[0] = startPosition;
    effect.Params[1] = endPosition;
    effect.Params[2] = strength;
    return effect;
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Vibration(uint8_t position, uint8_t amplitude, uint8_t frequency) {
    DualSenseTriggerEffect effect = {0};
    effect.Mode = 0x26;
    effect.Params[0] = 0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20; // All regions
    effect.Params[1] = 0x00;
    effect.Params[2] = 0x00;
    effect.Params[3] = 0x00;
    effect.Params[4] = amplitude;
    effect.Params[5] = frequency;
    effect.Params[6] = position;
    return effect;
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Bow(uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t snapForce) {
    DualSenseTriggerEffect effect = {0};
    effect.Mode = 0x26;
    effect.Params[0] = startPosition;
    effect.Params[1] = endPosition;
    effect.Params[2] = strength;
    effect.Params[3] = snapForce;
    effect.Params[4] = 0x00;
    effect.Params[5] = 0x00;
    effect.Params[6] = 0x00;
    effect.Params[7] = 0x00;
    return effect;
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Galloping(uint8_t startPosition, uint8_t endPosition, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency) {
    DualSenseTriggerEffect effect = {0};
    effect.Mode = 0x26;
    effect.Params[0] = startPosition;
    effect.Params[1] = endPosition;
    effect.Params[2] = firstFoot;
    effect.Params[3] = secondFoot;
    effect.Params[4] = frequency;
    return effect;
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::MachineGun(uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t frequency) {
    DualSenseTriggerEffect effect = {0};
    effect.Mode = 0x26;
    effect.Params[0] = startPosition;
    effect.Params[1] = endPosition;
    effect.Params[2] = strength;
    effect.Params[3] = frequency;
    return effect;
}