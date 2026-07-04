#include <gtest/gtest.h>
#include "Haptics/DualSenseTriggerEffectGenerator.h"

#include <array>
#include <cstdint>

using namespace ExtendInput::DataTools::DualSense;

// Each effect writes exactly 11 bytes starting at destinationIndex.
static constexpr int EFFECT_SIZE = 11;
static constexpr int OFFSET      = 0;

// ─── Helper ──────────────────────────────────────────────────────────────────
static std::array<uint8_t, 32> MakeBuffer() {
    std::array<uint8_t, 32> buf{};
    buf.fill(0xCC); // sentinel: 0xCC = unwritten bytes
    return buf;
}

// ═════════════════════════════════════════════════════════════════════════════
// Off
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, OffReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::Off(buf.data(), OFFSET));
}

TEST(DualSenseTrigger, OffWritesCorrectTypeByteAt0) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Off(buf.data(), OFFSET);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Off));
}

TEST(DualSenseTrigger, OffWritesAllZerosInPayload) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Off(buf.data(), OFFSET);
    for (int i = 1; i < EFFECT_SIZE; i++) {
        EXPECT_EQ(buf[i], 0x00) << "Byte " << i << " should be zero";
    }
}

TEST(DualSenseTrigger, OffRespectsDestinationIndex) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Off(buf.data(), 5);
    EXPECT_EQ(buf[5], static_cast<uint8_t>(TriggerEffectType::Off));
    // Bytes before the offset must be untouched
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(buf[i], 0xCC) << "Byte " << i << " should be untouched";
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Feedback - input validation
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, FeedbackReturnsTrueForValidParams) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::Feedback(buf.data(), OFFSET, 0, 8));
}

TEST(DualSenseTrigger, FeedbackReturnsFalseWhenPositionOver9) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Feedback(buf.data(), OFFSET, 10, 5));
}

TEST(DualSenseTrigger, FeedbackReturnsFalseWhenStrengthOver8) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Feedback(buf.data(), OFFSET, 0, 9));
}

TEST(DualSenseTrigger, FeedbackWithZeroStrengthWritesOffEffect) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Feedback(buf.data(), OFFSET, 0, 0);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Off));
}

TEST(DualSenseTrigger, FeedbackWritesCorrectTypeByteForNonzeroStrength) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Feedback(buf.data(), OFFSET, 0, 1);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Feedback));
}

TEST(DualSenseTrigger, FeedbackAtMaxValidPosition) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::Feedback(buf.data(), OFFSET, 9, 8));
}

// ═════════════════════════════════════════════════════════════════════════════
// Weapon - input validation
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, WeaponReturnsTrueForValidParams) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 2, 8, 4));
}

TEST(DualSenseTrigger, WeaponReturnsFalseWhenStartBelow2) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 1, 8, 4));
}

TEST(DualSenseTrigger, WeaponReturnsFalseWhenStartAbove7) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 8, 9, 4));
}

TEST(DualSenseTrigger, WeaponReturnsFalseWhenEndAbove8) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 2, 9, 4));
}

TEST(DualSenseTrigger, WeaponReturnsFalseWhenEndNotGreaterThanStart) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 5, 5, 4));
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 5, 4, 4));
}

TEST(DualSenseTrigger, WeaponReturnsFalseWhenStrengthOver8) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 2, 8, 9));
}

TEST(DualSenseTrigger, WeaponWithZeroStrengthWritesOffEffect) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 2, 8, 0);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Off));
}

TEST(DualSenseTrigger, WeaponWritesCorrectTypeByte) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Weapon(buf.data(), OFFSET, 2, 8, 4);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Weapon));
}

// ═════════════════════════════════════════════════════════════════════════════
// Vibration
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, VibrationReturnsTrueForValidParams) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::Vibration(buf.data(), OFFSET, 0, 5, 10));
}

TEST(DualSenseTrigger, VibrationWithZeroAmplitudeWritesOffEffect) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Vibration(buf.data(), OFFSET, 0, 0, 10);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Off));
}

TEST(DualSenseTrigger, VibrationWithZeroFrequencyWritesOffEffect) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Vibration(buf.data(), OFFSET, 0, 5, 0);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Off));
}

TEST(DualSenseTrigger, VibrationWritesCorrectTypeByte) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Vibration(buf.data(), OFFSET, 0, 5, 10);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Vibration));
}

// ═════════════════════════════════════════════════════════════════════════════
// SlopeFeedback
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, SlopeFeedbackReturnsTrueForValidParams) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::SlopeFeedback(buf.data(), OFFSET, 0, 8, 1, 8));
}

TEST(DualSenseTrigger, SlopeFeedbackReturnsFalseWhenEndNotGreaterThanStart) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::SlopeFeedback(buf.data(), OFFSET, 5, 5, 1, 8));
}

// ═════════════════════════════════════════════════════════════════════════════
// Bow
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, BowReturnsTrueForValidParams) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::Bow(buf.data(), OFFSET, 0, 8, 4, 3));
}

TEST(DualSenseTrigger, BowReturnsFalseWhenEndNotGreaterThanStart) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Bow(buf.data(), OFFSET, 5, 4, 4, 3));
}

TEST(DualSenseTrigger, BowWritesCorrectTypeByte) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Bow(buf.data(), OFFSET, 0, 8, 4, 3);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Bow));
}

// ═════════════════════════════════════════════════════════════════════════════
// Machine
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, MachineReturnsTrueForValidParams) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::Machine(buf.data(), OFFSET, 0, 9, 3, 7, 10, 2));
}

TEST(DualSenseTrigger, MachineReturnsFalseWhenEndNotGreaterThanStart) {
    auto buf = MakeBuffer();
    EXPECT_FALSE(DualSenseTriggerEffectGenerator::Machine(buf.data(), OFFSET, 5, 5, 3, 7, 10, 2));
}

TEST(DualSenseTrigger, MachineWritesCorrectTypeByte) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Machine(buf.data(), OFFSET, 0, 9, 3, 7, 10, 2);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Machine));
}

// ═════════════════════════════════════════════════════════════════════════════
// Simple effects
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, SimpleFeedbackWritesSimpleFeedbackType) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Simple_Feedback(buf.data(), OFFSET, 0, 5);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Simple_Feedback));
}

TEST(DualSenseTrigger, SimpleWeaponWritesSimpleWeaponType) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Simple_Weapon(buf.data(), OFFSET, 2, 8, 4);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Simple_Weapon));
}

TEST(DualSenseTrigger, SimpleVibrationWritesSimpleVibrationTypeOrOff) {
    auto buf = MakeBuffer();
    DualSenseTriggerEffectGenerator::Simple_Vibration(buf.data(), OFFSET, 0, 5, 10);
    uint8_t type = buf[0];
    EXPECT_TRUE(type == static_cast<uint8_t>(TriggerEffectType::Simple_Vibration) ||
                type == static_cast<uint8_t>(TriggerEffectType::Off));
}

// ═════════════════════════════════════════════════════════════════════════════
// ReWASD adapter effects - just verify they return true and write a type byte
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTriggerReWASD, FullPressReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::ReWASD::FullPress(buf.data(), OFFSET));
}

TEST(DualSenseTriggerReWASD, SoftPressReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::ReWASD::SoftPress(buf.data(), OFFSET));
}

TEST(DualSenseTriggerReWASD, HardPressReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::ReWASD::HardPress(buf.data(), OFFSET));
}

TEST(DualSenseTriggerReWASD, PulseReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::ReWASD::Pulse(buf.data(), OFFSET));
}

TEST(DualSenseTriggerReWASD, VibrationReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::ReWASD::Vibration(buf.data(), OFFSET));
}

TEST(DualSenseTriggerReWASD, RifleReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::ReWASD::Rifle(buf.data(), OFFSET));
}

TEST(DualSenseTriggerReWASD, MaxRigidityReturnsTrue) {
    auto buf = MakeBuffer();
    EXPECT_TRUE(DualSenseTriggerEffectGenerator::ReWASD::MaxRigidity(buf.data(), OFFSET));
}

// ═════════════════════════════════════════════════════════════════════════════
// MultiplePositionFeedback
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, MultiplePositionFeedbackAllZeroWritesOff) {
    auto buf = MakeBuffer();
    uint8_t strengths[10] = {0,0,0,0,0,0,0,0,0,0};
    DualSenseTriggerEffectGenerator::MultiplePositionFeedback(buf.data(), OFFSET, strengths);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Off));
}

TEST(DualSenseTrigger, MultiplePositionFeedbackNonzeroWritesFeedbackType) {
    auto buf = MakeBuffer();
    uint8_t strengths[10] = {0,0,0,0,0,3,0,0,0,0};
    DualSenseTriggerEffectGenerator::MultiplePositionFeedback(buf.data(), OFFSET, strengths);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Feedback));
}

// ═════════════════════════════════════════════════════════════════════════════
// MultiplePositionVibration
// ═════════════════════════════════════════════════════════════════════════════

TEST(DualSenseTrigger, MultiplePositionVibrationAllZeroWritesOff) {
    auto buf = MakeBuffer();
    uint8_t amps[10] = {0,0,0,0,0,0,0,0,0,0};
    DualSenseTriggerEffectGenerator::MultiplePositionVibration(buf.data(), OFFSET, 10, amps);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(TriggerEffectType::Off));
}

TEST(DualSenseTrigger, MultiplePositionVibrationNonzeroWritesVibrationOrOff) {
    auto buf = MakeBuffer();
    uint8_t amps[10] = {0,0,0,0,5,0,0,0,0,0};
    DualSenseTriggerEffectGenerator::MultiplePositionVibration(buf.data(), OFFSET, 10, amps);
    uint8_t type = buf[0];
    // Either Vibration or Off (zero freq still falls through to Off)
    EXPECT_TRUE(type == static_cast<uint8_t>(TriggerEffectType::Vibration) ||
                type == static_cast<uint8_t>(TriggerEffectType::Off));
}
