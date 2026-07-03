// test_input_source.cpp
//
// Unit tests for InputMapper::InputSource configuration semantics and the
// output-range / deadzone / invert post-processing logic used by
// InputMapper::ProcessSensor() and InputMapper::ProcessAxis().
//
// The post-processing pipeline is:
//   1. Apply invert  (raw = -raw)
//   2. Apply deadzone  (|raw| < deadzone → 0)
//   3. Clamp to [-1, 1]
//   4. Remap by outputRange:
//        0 → [-1, +1]  (pass-through)
//        1 → [ 0, +1]  ((r+1)/2)
//        2 → [-1,  0]  ((r-1)/2)
//        5 → [customMin, customMax]  (user-defined span, linear remap)
//
// Because this pipeline is a pure function of (raw, invert, deadzone,
// outputRange, customMin, customMax), we replicate it here as a free
// function and test it in isolation - no SDL, no DeviceManager, no
// InputMapper instantiation required.
//
// We also test the SensorChannel enum ordering and default-value invariants
// that the serialisation code relies on.

#include <gtest/gtest.h>
#include "Mappers/InputMapper.h"
#include <cmath>
#include <algorithm>

// ─── Replicated post-processing from InputMapper::ProcessSensor ──────────────

static float ApplyInputSourcePostProcess(float raw,
                                          bool  invert,
                                          float deadzone,
                                          int   outputRange,
                                          float customMin = -1.f,
                                          float customMax = 1.f)
{
    if (invert) raw = -raw;
    float norm = raw;
    if (std::abs(norm) < deadzone) {
        norm = 0.f;
    } else {
        norm = norm > 0 ? (norm - deadzone) / (1.f - deadzone) : (norm + deadzone) / (1.f - deadzone);
    }
    float r = std::clamp(norm, -1.f, 1.f);
    if      (outputRange == 1) r = (r + 1.f) * 0.5f;
    else if (outputRange == 2) r = (r - 1.f) * 0.5f;
    else if (outputRange == 3) r = std::max(r, 0.f);
    else if (outputRange == 4) r = std::max(-r, 0.f);
    else if (outputRange == 5) r = customMin + (r + 1.f) * 0.5f * (customMax - customMin);
    return r;
}

// Convenience: build a minimal InputSource with only the post-process fields set.
static InputMapper::InputSource MakeSource(bool invert, float deadzone, int outputRange)
{
    InputMapper::InputSource s;
    s.invert      = invert;
    s.deadzone    = deadzone;
    s.outputRange = outputRange;
    return s;
}

// ═════════════════════════════════════════════════════════════════════════════
// InputSource defaults
// ═════════════════════════════════════════════════════════════════════════════

TEST(InputSource, DefaultConstruction) {
    InputMapper::InputSource s;
    EXPECT_EQ(s.axisIndex,    -1);
    EXPECT_FALSE(s.invert);
    EXPECT_FLOAT_EQ(s.deadzone, 0.05f);
    EXPECT_EQ(s.outputRange, 0);
    EXPECT_EQ(s.sensorChannel, InputMapper::InputSource::SensorChannel::None);
    EXPECT_EQ(s.instance_id,  (SDL_JoystickID)0);
}

TEST(InputSource, DefaultDeadzoneIsSmallPositive) {
    InputMapper::InputSource s;
    EXPECT_GT(s.deadzone, 0.f);
    EXPECT_LT(s.deadzone, 0.1f);
}

// ═════════════════════════════════════════════════════════════════════════════
// SensorChannel enum - ordering and None sentinel
// ═════════════════════════════════════════════════════════════════════════════

using SC = InputMapper::InputSource::SensorChannel;

TEST(SensorChannel, NoneIsZero) {
    EXPECT_EQ(static_cast<int>(SC::None), 0);
}

TEST(SensorChannel, GyroTrioIsConsecutive) {
    int x = static_cast<int>(SC::GyroX);
    EXPECT_EQ(static_cast<int>(SC::GyroY), x + 1);
    EXPECT_EQ(static_cast<int>(SC::GyroZ), x + 2);
}

TEST(SensorChannel, AccelTrioIsConsecutive) {
    int x = static_cast<int>(SC::AccelX);
    EXPECT_EQ(static_cast<int>(SC::AccelY), x + 1);
    EXPECT_EQ(static_cast<int>(SC::AccelZ), x + 2);
}

TEST(SensorChannel, GyroLTrioIsConsecutive) {
    int x = static_cast<int>(SC::GyroLX);
    EXPECT_EQ(static_cast<int>(SC::GyroLY), x + 1);
    EXPECT_EQ(static_cast<int>(SC::GyroLZ), x + 2);
}

TEST(SensorChannel, AccelLTrioIsConsecutive) {
    int x = static_cast<int>(SC::AccelLX);
    EXPECT_EQ(static_cast<int>(SC::AccelLY), x + 1);
    EXPECT_EQ(static_cast<int>(SC::AccelLZ), x + 2);
}

TEST(SensorChannel, GyroRTrioIsConsecutive) {
    int x = static_cast<int>(SC::GyroRX);
    EXPECT_EQ(static_cast<int>(SC::GyroRY), x + 1);
    EXPECT_EQ(static_cast<int>(SC::GyroRZ), x + 2);
}

TEST(SensorChannel, AccelRTrioIsConsecutive) {
    int x = static_cast<int>(SC::AccelRX);
    EXPECT_EQ(static_cast<int>(SC::AccelRY), x + 1);
    EXPECT_EQ(static_cast<int>(SC::AccelRZ), x + 2);
}

TEST(SensorChannel, Touch2XBeforeTouch2Y) {
    EXPECT_LT(static_cast<int>(SC::Touch2X), static_cast<int>(SC::Touch2Y));
}

TEST(SensorChannel, CapSenseChannelsAreDistinct) {
    // All four cap-sense channels must be unique values.
    int ls = static_cast<int>(SC::LeftStickTouch);
    int rs = static_cast<int>(SC::RightStickTouch);
    int lg = static_cast<int>(SC::LeftGripTouch);
    int rg = static_cast<int>(SC::RightGripTouch);
    EXPECT_NE(ls, rs);
    EXPECT_NE(ls, lg);
    EXPECT_NE(ls, rg);
    EXPECT_NE(rs, lg);
    EXPECT_NE(rs, rg);
    EXPECT_NE(lg, rg);
}

TEST(SensorChannel, CapSenseSeparateFromGyro) {
    EXPECT_NE(static_cast<int>(SC::LeftStickTouch),  static_cast<int>(SC::GyroX));
    EXPECT_NE(static_cast<int>(SC::RightStickTouch), static_cast<int>(SC::GyroX));
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - no invert, no deadzone, range 0 (identity path)
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, PassThroughMidValue) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.5f, false, 0.f, 0), 0.5f);
}

TEST(PostProcess, PassThroughNegativeValue) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-0.5f, false, 0.f, 0), -0.5f);
}

TEST(PostProcess, PassThroughZero) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.f, false, 0.f, 0), 0.f);
}

TEST(PostProcess, PassThroughPlusOne) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(1.f, false, 0.f, 0), 1.f);
}

TEST(PostProcess, PassThroughMinusOne) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-1.f, false, 0.f, 0), -1.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - clamp behaviour
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, ClampAbovePlusOne) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(2.f, false, 0.f, 0), 1.f);
}

TEST(PostProcess, ClampBelowMinusOne) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-2.f, false, 0.f, 0), -1.f);
}

TEST(PostProcess, ClampLargePositive) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(100.f, false, 0.f, 0), 1.f);
}

TEST(PostProcess, ClampLargeNegative) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-100.f, false, 0.f, 0), -1.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - invert flag
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, InvertFlipsSign) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.6f, true, 0.f, 0), -0.6f);
}

TEST(PostProcess, InvertFlipsNegativeToPositive) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-0.4f, true, 0.f, 0), 0.4f);
}

TEST(PostProcess, InvertZeroStaysZero) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.f, true, 0.f, 0), 0.f);
}

TEST(PostProcess, InvertPlusOneGivesMinusOne) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(1.f, true, 0.f, 0), -1.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - deadzone
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, DeadzoneZeroesSmallPositiveValue) {
    // Value 0.03 is below the default 0.05 deadzone.
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.03f, false, 0.05f, 0), 0.f);
}

TEST(PostProcess, DeadzoneZeroesSmallNegativeValue) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-0.03f, false, 0.05f, 0), 0.f);
}

TEST(PostProcess, DeadzonePassesValueAtBoundary) {
    // Exactly at deadzone: |raw| == deadzone.
    // Remap logic: (0.05 - 0.05) / (1.0 - 0.05) = 0.0.
    // This ensures a smooth transition from 0 exactly at the threshold.
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.05f, false, 0.05f, 0), 0.0f);
}

TEST(PostProcess, DeadzonePassesValueAboveBoundary) {
    EXPECT_NEAR(ApplyInputSourcePostProcess(0.1f, false, 0.05f, 0), 0.0526316f, 0.0001f);
}

TEST(PostProcess, DeadzoneZeroSuppressesNothing) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.01f, false, 0.f, 0), 0.01f);
}

TEST(PostProcess, InvertAppliedBeforeDeadzone) {
    // raw=0.03, invert=true → raw becomes -0.03.  |raw|=0.03 < deadzone=0.05 → 0.
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.03f, true, 0.05f, 0), 0.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - output range 1  ([-1,+1] → [0,+1])
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, Range1MinusOneBecomesZero) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-1.f, false, 0.f, 1), 0.f);
}

TEST(PostProcess, Range1PlusOneBecomesOne) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(1.f, false, 0.f, 1), 1.f);
}

TEST(PostProcess, Range1ZeroBecomes0_5) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.f, false, 0.f, 1), 0.5f);
}

TEST(PostProcess, Range1MidNegBecomes0_25) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-0.5f, false, 0.f, 1), 0.25f);
}

TEST(PostProcess, Range1MidPosBecomes0_75) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.5f, false, 0.f, 1), 0.75f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - output range 2  ([-1,+1] → [-1, 0])
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, Range2MinusOneBecomesMinusOne) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-1.f, false, 0.f, 2), -1.f);
}

TEST(PostProcess, Range2PlusOneBecomesZero) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(1.f, false, 0.f, 2), 0.f);
}

TEST(PostProcess, Range2ZeroBecomesMinus0_5) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.f, false, 0.f, 2), -0.5f);
}

TEST(PostProcess, Range2MidPosBecomesMinus0_25) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.5f, false, 0.f, 2), -0.25f);
}

TEST(PostProcess, Range2MidNegBecomesMinus0_75) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-0.5f, false, 0.f, 2), -0.75f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - output range 5  (custom [customMin, customMax])
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, Range5MinusOneBecomesCustomMin) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-1.f, false, 0.f, 5, -10.f, 10.f), -10.f);
}

TEST(PostProcess, Range5PlusOneBecomesCustomMax) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(1.f, false, 0.f, 5, -10.f, 10.f), 10.f);
}

TEST(PostProcess, Range5ZeroBecomesMidpoint) {
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.f, false, 0.f, 5, -10.f, 10.f), 0.f);
}

TEST(PostProcess, Range5AsymmetricBounds) {
    // customMin=0, customMax=100: raw=0.5 -> r=0.5 -> 0 + (0.5+1)/2*100 = 75
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.5f, false, 0.f, 5, 0.f, 100.f), 75.f);
}

TEST(PostProcess, Range5MatchesDefaultRangeWhenBoundsAreMinusOneToOne) {
    // customMin=-1, customMax=1 should behave identically to outputRange 0.
    for (float raw : {-1.f, -0.5f, 0.f, 0.5f, 1.f}) {
        EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(raw, false, 0.f, 5, -1.f, 1.f),
                         ApplyInputSourcePostProcess(raw, false, 0.f, 0));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// InputSource - custom range field defaults
// ═════════════════════════════════════════════════════════════════════════════

TEST(InputSource, CustomRangeDefaultsMatchLegacyMinusOneToOne) {
    InputMapper::InputSource s;
    EXPECT_FLOAT_EQ(s.customRangeMin, -1.f);
    EXPECT_FLOAT_EQ(s.customRangeMax, 1.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Post-processing - combined invert + range
// ═════════════════════════════════════════════════════════════════════════════

TEST(PostProcess, InvertWithRange1) {
    // raw=0.5 → inverted=-0.5 → range1: (-0.5+1)/2 = 0.25
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.5f, true, 0.f, 1), 0.25f);
}

TEST(PostProcess, InvertWithRange2) {
    // raw=-0.5 → inverted=0.5 → range2: (0.5-1)/2 = -0.25
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(-0.5f, true, 0.f, 2), -0.25f);
}

TEST(PostProcess, DeadzoneWithRange1_ValueAtBoundaryIsZeroThenMapped) {
    // raw=0.05 == deadzone=0.05 → 0 → range1: (0+1)/2 = 0.5
    EXPECT_FLOAT_EQ(ApplyInputSourcePostProcess(0.05f, false, 0.05f, 1), 0.5f);
}

// ═════════════════════════════════════════════════════════════════════════════
// ButtonToDigitalMapping - default construction
// ═════════════════════════════════════════════════════════════════════════════

TEST(ButtonToDigitalMapping, DefaultMode) {
    InputMapper::ButtonToDigitalMapping m;
    EXPECT_EQ(m.mode, InputMapper::ButtonToDigitalMapping::Mode::Momentary);
}

TEST(ButtonToDigitalMapping, DefaultButtonIndex) {
    InputMapper::ButtonToDigitalMapping m;
    EXPECT_EQ(m.button_index, -1);
}

TEST(ButtonToDigitalMapping, DefaultHatIndex) {
    InputMapper::ButtonToDigitalMapping m;
    EXPECT_EQ(m.hat_index, -1);
    EXPECT_EQ(m.hat_mask,  0);
}

TEST(ButtonToDigitalMapping, DefaultSensorChannelIsNone) {
    InputMapper::ButtonToDigitalMapping m;
    EXPECT_EQ(m.sensor_channel, InputMapper::InputSource::SensorChannel::None);
}

TEST(ButtonToDigitalMapping, DefaultLastPhysicalStateIsFalse) {
    InputMapper::ButtonToDigitalMapping m;
    EXPECT_FALSE(m.last_physical_state);
}

// ═════════════════════════════════════════════════════════════════════════════
// ButtonToAnalogMapping - default construction
// ═════════════════════════════════════════════════════════════════════════════

TEST(ButtonToAnalogMapping, DefaultOnOffValues) {
    InputMapper::ButtonToAnalogMapping m;
    EXPECT_FLOAT_EQ(m.on_value,  1.f);
    EXPECT_FLOAT_EQ(m.off_value, 0.f);
}

TEST(ButtonToAnalogMapping, DefaultButtonIndex) {
    InputMapper::ButtonToAnalogMapping m;
    EXPECT_EQ(m.button_index, -1);
}

TEST(ButtonToAnalogMapping, DefaultSensorChannelIsNone) {
    InputMapper::ButtonToAnalogMapping m;
    EXPECT_EQ(m.sensor_channel, InputMapper::InputSource::SensorChannel::None);
}