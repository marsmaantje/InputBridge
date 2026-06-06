// test_sensor_state.cpp
//
// Unit tests for the GyroState, AccelState, and TouchState value-type structs
// declared in Devices/SensorState.h.
//
// These structs have no SDL dependency: they are plain-old-data types whose
// only logic lives in a few inline helper methods on TouchState.  The tests
// cover:
//   • Default-construction invariants
//   • Scale constants (used by SensorReader to normalise raw SDL data)
//   • TouchState convenience accessors (primaryX/Y/Pressure, centered mapping)
//   • Finger activity gating (inactive finger → zero output)
//   • Multi-finger edge cases (both fingers active / inactive)

#include <gtest/gtest.h>
#include "Devices/SensorState.h"

// ═════════════════════════════════════════════════════════════════════════════
// GyroState
// ═════════════════════════════════════════════════════════════════════════════

TEST(GyroState, DefaultConstructionIsZeroUnavailable) {
    GyroState g;
    EXPECT_FLOAT_EQ(g.x, 0.f);
    EXPECT_FLOAT_EQ(g.y, 0.f);
    EXPECT_FLOAT_EQ(g.z, 0.f);
    EXPECT_FALSE(g.available);
}

TEST(GyroState, ScaleConstantIsPositive) {
    EXPECT_GT(GyroState::SCALE, 0.f);
}

TEST(GyroState, ScaleMatchesExpectedValue) {
    // The DualSense gyro document specifies ±20 rad/s as the normalisation
    // range used by SensorReader.  Catching accidental changes here keeps the
    // mapping between raw SDL units and the [-1,1] output stable.
    EXPECT_FLOAT_EQ(GyroState::SCALE, 20.f);
}

TEST(GyroState, AvailableFlagCanBeSet) {
    GyroState g;
    g.available = true;
    EXPECT_TRUE(g.available);
}

TEST(GyroState, FieldsAreIndependent) {
    GyroState g;
    g.x = 0.5f;
    EXPECT_FLOAT_EQ(g.x, 0.5f);
    EXPECT_FLOAT_EQ(g.y, 0.f);
    EXPECT_FLOAT_EQ(g.z, 0.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// AccelState
// ═════════════════════════════════════════════════════════════════════════════

TEST(AccelState, DefaultConstructionIsZeroUnavailable) {
    AccelState a;
    EXPECT_FLOAT_EQ(a.x, 0.f);
    EXPECT_FLOAT_EQ(a.y, 0.f);
    EXPECT_FLOAT_EQ(a.z, 0.f);
    EXPECT_FALSE(a.available);
}

TEST(AccelState, ScaleConstantIsPositive) {
    EXPECT_GT(AccelState::SCALE, 0.f);
}

TEST(AccelState, ScaleMatchesExpectedValue) {
    // ±20 m/s² so that 1 g (≈9.81 m/s²) maps to roughly ±0.49 — well within
    // the [-1,1] range without saturating at rest.
    EXPECT_FLOAT_EQ(AccelState::SCALE, 20.f);
}

TEST(AccelState, GyroAndAccelScalesAreEqual) {
    // Both sensors share the same scale constant by design.
    EXPECT_FLOAT_EQ(GyroState::SCALE, AccelState::SCALE);
}

// ═════════════════════════════════════════════════════════════════════════════
// TouchFingerState
// ═════════════════════════════════════════════════════════════════════════════

TEST(TouchFingerState, DefaultConstructionIsInactiveZero) {
    TouchFingerState f;
    EXPECT_FALSE(f.active);
    EXPECT_FLOAT_EQ(f.x, 0.f);
    EXPECT_FLOAT_EQ(f.y, 0.f);
    EXPECT_FLOAT_EQ(f.pressure, 0.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// TouchState — default construction
// ═════════════════════════════════════════════════════════════════════════════

TEST(TouchState, DefaultConstructionIsUnavailable) {
    TouchState t;
    EXPECT_FALSE(t.available);
}

TEST(TouchState, DefaultFingerArrayIsInactiveZero) {
    TouchState t;
    for (const auto& f : t.fingers) {
        EXPECT_FALSE(f.active);
        EXPECT_FLOAT_EQ(f.x, 0.f);
        EXPECT_FLOAT_EQ(f.y, 0.f);
        EXPECT_FLOAT_EQ(f.pressure, 0.f);
    }
}

TEST(TouchState, FingerArraySizeIsTwo) {
    TouchState t;
    EXPECT_EQ(t.fingers.size(), 2u);
}

// ═════════════════════════════════════════════════════════════════════════════
// TouchState::primaryX/Y/Pressure — inactive finger → zero
// ═════════════════════════════════════════════════════════════════════════════

TEST(TouchState, PrimaryXIsZeroWhenFingerInactive) {
    TouchState t;
    t.fingers[0].active = false;
    t.fingers[0].x = 0.75f; // Would be non-zero if returned
    EXPECT_FLOAT_EQ(t.primaryX(), 0.f);
}

TEST(TouchState, PrimaryYIsZeroWhenFingerInactive) {
    TouchState t;
    t.fingers[0].active = false;
    t.fingers[0].y = 0.9f;
    EXPECT_FLOAT_EQ(t.primaryY(), 0.f);
}

TEST(TouchState, PrimaryPressureIsZeroWhenFingerInactive) {
    TouchState t;
    t.fingers[0].active = false;
    t.fingers[0].pressure = 0.5f;
    EXPECT_FLOAT_EQ(t.primaryPressure(), 0.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// TouchState::primaryX/Y/Pressure — active finger → real value
// ═════════════════════════════════════════════════════════════════════════════

TEST(TouchState, PrimaryXReturnsValueWhenFingerActive) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 0.25f;
    EXPECT_FLOAT_EQ(t.primaryX(), 0.25f);
}

TEST(TouchState, PrimaryYReturnsValueWhenFingerActive) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].y = 0.8f;
    EXPECT_FLOAT_EQ(t.primaryY(), 0.8f);
}

TEST(TouchState, PrimaryPressureReturnsValueWhenFingerActive) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].pressure = 0.6f;
    EXPECT_FLOAT_EQ(t.primaryPressure(), 0.6f);
}

// ═════════════════════════════════════════════════════════════════════════════
// TouchState::primaryXCentered / primaryYCentered
// SDL gives [0,1]; the centered helpers remap to [-1, +1].
// ═════════════════════════════════════════════════════════════════════════════

TEST(TouchState, CenteredXAtLeftEdgeIsMinus1) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 0.f;
    EXPECT_FLOAT_EQ(t.primaryXCentered(), -1.f);
}

TEST(TouchState, CenteredXAtRightEdgeIsPlus1) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 1.f;
    EXPECT_FLOAT_EQ(t.primaryXCentered(), 1.f);
}

TEST(TouchState, CenteredXAtMidpointIsZero) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 0.5f;
    EXPECT_FLOAT_EQ(t.primaryXCentered(), 0.f);
}

TEST(TouchState, CenteredYAtTopEdgeIsMinus1) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].y = 0.f;
    EXPECT_FLOAT_EQ(t.primaryYCentered(), -1.f);
}

TEST(TouchState, CenteredYAtBottomEdgeIsPlus1) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].y = 1.f;
    EXPECT_FLOAT_EQ(t.primaryYCentered(), 1.f);
}

TEST(TouchState, CenteredYAtMidpointIsZero) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].y = 0.5f;
    EXPECT_FLOAT_EQ(t.primaryYCentered(), 0.f);
}

TEST(TouchState, CenteredXIsZeroWhenFingerInactive) {
    // When the finger is not touching, centered getters should return 0 (neutral)
    // rather than -1 (the result of remapping a zero raw coordinate).
    TouchState t;
    t.fingers[0].active = false;
    t.fingers[0].x = 0.5f;
    EXPECT_FLOAT_EQ(t.primaryXCentered(), 0.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Boundary / quarter-point values for centering formula
// ═════════════════════════════════════════════════════════════════════════════

TEST(TouchState, CenteredXAtQuarterIs_Minus0_5) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 0.25f;
    EXPECT_FLOAT_EQ(t.primaryXCentered(), -0.5f);
}

TEST(TouchState, CenteredXAtThreeQuartersIs_Plus0_5) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 0.75f;
    EXPECT_FLOAT_EQ(t.primaryXCentered(), 0.5f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Second finger independence
// ═════════════════════════════════════════════════════════════════════════════

TEST(TouchState, SecondFingerDefaultIsInactive) {
    TouchState t;
    EXPECT_FALSE(t.fingers[1].active);
    EXPECT_FLOAT_EQ(t.fingers[1].x, 0.f);
    EXPECT_FLOAT_EQ(t.fingers[1].y, 0.f);
}

TEST(TouchState, SecondFingerActiveDoesNotAffectPrimary) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 0.3f;
    t.fingers[1].active = true;
    t.fingers[1].x = 0.9f;
    EXPECT_FLOAT_EQ(t.primaryX(), 0.3f);
}

TEST(TouchState, BothFingersActiveIndependently) {
    TouchState t;
    t.fingers[0].active = true;
    t.fingers[0].x = 0.1f;
    t.fingers[0].y = 0.2f;
    t.fingers[1].active = true;
    t.fingers[1].x = 0.8f;
    t.fingers[1].y = 0.9f;
    EXPECT_FLOAT_EQ(t.fingers[0].x, 0.1f);
    EXPECT_FLOAT_EQ(t.fingers[1].x, 0.8f);
}

TEST(TouchState, PrimaryAccessorsIgnoreSecondFinger) {
    TouchState t;
    t.fingers[0].active = false; // primary inactive
    t.fingers[1].active = true;
    t.fingers[1].x = 0.5f;
    // primaryX() must return 0 regardless of second finger
    EXPECT_FLOAT_EQ(t.primaryX(), 0.f);
}

// ═════════════════════════════════════════════════════════════════════════════
// SensorCapabilities
// ═════════════════════════════════════════════════════════════════════════════

#include "Devices/SensorReader.h"

TEST(SensorCapabilities, DefaultConstructionAllFalse) {
    SensorCapabilities caps;
    EXPECT_FALSE(caps.gyro);
    EXPECT_FALSE(caps.accel);
    EXPECT_FALSE(caps.gyroL);
    EXPECT_FALSE(caps.accelL);
    EXPECT_FALSE(caps.gyroR);
    EXPECT_FALSE(caps.accelR);
    EXPECT_FALSE(caps.touch);
    EXPECT_FALSE(caps.capSenseLeftStick);
    EXPECT_FALSE(caps.capSenseRightStick);
    EXPECT_FALSE(caps.capSenseLeftGrip);
    EXPECT_FALSE(caps.capSenseRightGrip);
}

TEST(SensorCapabilities, HasAnyFalseWhenAllFalse) {
    SensorCapabilities caps;
    EXPECT_FALSE(caps.HasAny());
}

TEST(SensorCapabilities, HasAnyTrueForGyro) {
    SensorCapabilities caps;
    caps.gyro = true;
    EXPECT_TRUE(caps.HasAny());
}

TEST(SensorCapabilities, HasAnyTrueForTouch) {
    SensorCapabilities caps;
    caps.touch = true;
    EXPECT_TRUE(caps.HasAny());
}

TEST(SensorCapabilities, HasAnyTrueForCapSense) {
    SensorCapabilities caps;
    caps.capSenseLeftGrip = true;
    EXPECT_TRUE(caps.HasAny());
}

TEST(SensorCapabilities, HasAnyTrueForAccelROnly) {
    SensorCapabilities caps;
    caps.accelR = true;
    EXPECT_TRUE(caps.HasAny());
}

TEST(SensorCapabilities, AllFlagsIndependent) {
    SensorCapabilities caps;
    caps.gyroL = true;
    EXPECT_TRUE(caps.gyroL);
    EXPECT_FALSE(caps.gyroR);
    EXPECT_FALSE(caps.gyro);
}
