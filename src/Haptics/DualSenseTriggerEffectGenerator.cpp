#include "DualSenseTriggerEffectGenerator.h"
#include <cstring>
#include <algorithm>

void DualSenseTriggerEffectGenerator::ApplyToBuffer(uint8_t* buffer, std::size_t offset, const DualSenseTriggerEffect& effect) {
    buffer[offset] = effect.Mode;
    std::copy(effect.Params.begin(), effect.Params.end(), buffer + offset + 1);
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Off() {
    return CreateOff();
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Feedback(uint8_t startPosition, uint8_t strength) {
    return CreateFeedback(startPosition, strength);
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Weapon(uint8_t startPosition, uint8_t endPosition, uint8_t strength) {
    return CreateWeapon(startPosition, endPosition, strength);
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Vibration(uint8_t position, uint8_t amplitude, uint8_t frequency) {
    DualSenseTriggerEffect effect{};
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
    return CreateBow(startPosition, endPosition, strength, snapForce);
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::Galloping(uint8_t startPosition, uint8_t endPosition, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency) {
    return CreateGalloping(startPosition, endPosition, firstFoot, secondFoot, frequency);
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::MachineGun(uint8_t startPosition, uint8_t endPosition, uint8_t strength, uint8_t frequency) {
    // The gist's CreateAutomatic takes period instead of frequency. Assuming they are meant to be the same for this mapping.
    return CreateAutomatic(startPosition, endPosition, strength, frequency);
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateEffect(TriggerEffect effect, const std::vector<uint8_t> &parameters) {
    return CreateEffect(static_cast<uint8_t>(effect), parameters);
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateEffect(uint8_t effect, const std::vector<uint8_t> &parameters) {
    DualSenseTriggerEffect result{};
    result.Mode = effect;
    if (!parameters.empty()) {
        std::copy_n(parameters.begin(), std::min(parameters.size(), result.Params.size()), result.Params.begin());
    }
    return result;
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateOff() {
    return CreateEffect(TriggerEffect::Off, {});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateRigid(uint8_t start, uint8_t force) {
    return CreateEffect(TriggerEffect::Rigid, {start, force});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateRigid_A(uint8_t start, uint8_t force) {
    return CreateEffect(TriggerEffect::Rigid_A, {start, force, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateRigid_B(uint8_t start, uint8_t force) {
    return CreateEffect(TriggerEffect::Rigid_B, {0x00, 0x00, start, force, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateRigid_AB(uint8_t start, uint8_t force) {
    return CreateEffect(TriggerEffect::Rigid_AB, {start, force, start, force, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreatePulse(uint8_t start, uint8_t end, uint8_t force) {
    return CreateEffect(TriggerEffect::Pulse, {start, end, force});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreatePulse_A(uint8_t start, uint8_t end, uint8_t force) {
    return CreateEffect(TriggerEffect::Pulse_A, {start, end, force, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreatePulse_B(uint8_t start, uint8_t end, uint8_t force) {
    return CreateEffect(TriggerEffect::Pulse_B, {0x00, 0x00, 0x00, start, end, force, 0x00, 0x00, 0x00, 0x00});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreatePulse_AB(uint8_t start, uint8_t end, uint8_t force) {
    return CreateEffect(TriggerEffect::Pulse_AB, {start, end, force, start, end, force, 0x00, 0x00, 0x00, 0x00});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateFeedback(uint8_t position, uint8_t strength) {
    return CreateEffect(TriggerEffect::Feedback, {position, strength});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateWeapon(uint8_t start, uint8_t end, uint8_t strength) {
    return CreateEffect(TriggerEffect::Weapon, {start, end, strength});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateBow(uint8_t start, uint8_t end, uint8_t strength, uint8_t snapForce) {
    return CreateEffect(TriggerEffect::Bow, {start, end, strength, snapForce});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateGalloping(uint8_t start, uint8_t end, uint8_t firstFoot, uint8_t secondFoot, uint8_t frequency) {
    return CreateEffect(TriggerEffect::Galloping, {start, end, firstFoot, secondFoot, frequency});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateSemiAutomatic(uint8_t start, uint8_t end, uint8_t strength) {
    return CreateEffect(TriggerEffect::SemiAutomatic, {start, end, strength});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateAutomatic(uint8_t start, uint8_t end, uint8_t strength, uint8_t period) {
    return CreateEffect(TriggerEffect::Automatic, {start, end, strength, period});
}

DualSenseTriggerEffect DualSenseTriggerEffectGenerator::CreateMachine(uint8_t start, uint8_t strength, uint8_t frequency, uint8_t period) {
    return CreateEffect(TriggerEffect::Machine, {start, 0x00, strength, frequency, 0x00, 0x00, period});
}
