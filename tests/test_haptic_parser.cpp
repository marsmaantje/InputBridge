#include <gtest/gtest.h>
#include "Network/HapticParser.h"
#include "mocks/output_mapper_stub.h"

// ─── Fake mapper pointer ──────────────────────────────────────────────────────
// HapticParser::Parse() only requires a non-null OutputMapper*.
// The stub's Queue methods never dereference 'this' — they only write to the
// HapticStub global vectors.  We pass the address of a static intptr_t as
// the sentinel; no OutputMapper is ever constructed.
// A non-null sentinel address; the stub Queue* methods never dereference 'this'.
// We use intptr_t to avoid strict-aliasing UB and keep the cast well-defined.
static intptr_t g_mapper_sentinel = 0;
static OutputMapper* FakeMapper() {
    return reinterpret_cast<OutputMapper*>(&g_mapper_sentinel);
}

// ─── Fixture ─────────────────────────────────────────────────────────────────

class HapticParserTest : public ::testing::Test {
protected:
    void SetUp() override    { HapticStub::Reset(); }
    void TearDown() override { HapticStub::Reset(); }
};

// ═════════════════════════════════════════════════════════════════════════════
// Null mapper / bad input guards
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, NullMapperDoesNotCrash) {
    HapticParser::Parse(R"({"type":"haptic","effect":"rumble"})", nullptr);
    EXPECT_TRUE(HapticStub::rumbleCalls.empty());
}

TEST_F(HapticParserTest, EmptyStringProducesNoCalls) {
    HapticParser::Parse("", FakeMapper());
    EXPECT_TRUE(HapticStub::rumbleCalls.empty());
}

TEST_F(HapticParserTest, PlainTextIsIgnored) {
    HapticParser::Parse("not json at all", FakeMapper());
    EXPECT_TRUE(HapticStub::rumbleCalls.empty());
}

TEST_F(HapticParserTest, EmptyObjectProducesNoCalls) {
    HapticParser::Parse("{}", FakeMapper());
    EXPECT_TRUE(HapticStub::rumbleCalls.empty());
}

TEST_F(HapticParserTest, UnknownTypeIsIgnored) {
    HapticParser::Parse(R"({"type":"joystick","effect":"rumble","device":0})", FakeMapper());
    EXPECT_TRUE(HapticStub::rumbleCalls.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// Rumble — all three accepted type strings
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, TypeHapticRumbleCallsQueueRumble) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,
            "params":{"low":0.5,"high":0.3,"duration":200}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
}

TEST_F(HapticParserTest, TypeGamepadRumbleIsAccepted) {
    HapticParser::Parse(
        R"({"type":"gamepad","effect":"rumble","device":0,
            "params":{"low":0.1,"high":0.1,"duration":100}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
}

TEST_F(HapticParserTest, TypeSteeringWheelRumbleIsAccepted) {
    HapticParser::Parse(
        R"({"type":"steering_wheel","effect":"rumble","device":0,
            "params":{"low":1.0,"high":1.0,"duration":500}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Rumble — field values
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, RumbleLowHighDurationParsedCorrectly) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":2,
            "params":{"low":0.7,"high":0.4,"duration":300}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    const auto& c = HapticStub::rumbleCalls[0];
    EXPECT_EQ(c.device,   2);
    EXPECT_EQ(c.slot,     0);   // no "slot" key → defaults to 0
    EXPECT_FLOAT_EQ(c.low,   0.7f);
    EXPECT_FLOAT_EQ(c.high,  0.4f);
    EXPECT_EQ(c.duration, 300);
}

TEST_F(HapticParserTest, RumbleLargeMagnitudeOverridesLow) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,
            "params":{"low":0.1,"large_magnitude":0.9,"high":0.2,"duration":100}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::rumbleCalls[0].low, 0.9f);
}

TEST_F(HapticParserTest, RumbleSmallMagnitudeOverridesHigh) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,
            "params":{"high":0.1,"small_magnitude":0.8,"duration":100}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::rumbleCalls[0].high, 0.8f);
}

TEST_F(HapticParserTest, RumbleDurationMsOverridesDuration) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,
            "params":{"low":0.5,"high":0.5,"duration":100,"duration_ms":750}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_EQ(HapticStub::rumbleCalls[0].duration, 750);
}

TEST_F(HapticParserTest, RumbleDefaultsWhenFieldsAbsent) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,"params":{}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    const auto& c = HapticStub::rumbleCalls[0];
    EXPECT_FLOAT_EQ(c.low,  0.0f);
    EXPECT_FLOAT_EQ(c.high, 0.0f);
    EXPECT_EQ(c.duration,   0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Constant force
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, ConstantEffectCallsQueueConstantForce) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"constant","device":1,
            "params":{"strength":0.6,"duration":400}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    const auto& c = HapticStub::constantCalls[0];
    EXPECT_EQ(c.device, 1);
    EXPECT_EQ(c.slot,   0);   // no "slot" key → defaults to 0
    EXPECT_FLOAT_EQ(c.strength, 0.6f);
    EXPECT_EQ(c.duration, 400);
}

TEST_F(HapticParserTest, ConstantDurationMsAlias) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"constant","device":0,
            "params":{"strength":0.5,"duration":100,"duration_ms":999}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    EXPECT_EQ(HapticStub::constantCalls[0].duration, 999);
}

TEST_F(HapticParserTest, ConstantDefaultsWhenFieldsAbsent) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"constant","device":0,"params":{}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::constantCalls[0].strength, 0.0f);
    EXPECT_EQ(HapticStub::constantCalls[0].duration,       0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Periodic
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, PeriodicEffectCallsQueuePeriodic) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"periodic","device":0,
            "params":{"strength":0.8,"period":50,"magnitude":0.9,
                      "offset":0.1,"phase":45,"duration":600}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::periodicCalls.size(), 1u);
    const auto& c = HapticStub::periodicCalls[0];
    EXPECT_EQ(c.slot,             0);   // no "slot" key → defaults to 0
    EXPECT_FLOAT_EQ(c.strength,   0.8f);
    EXPECT_EQ(c.period,           50);
    EXPECT_FLOAT_EQ(c.magnitude,  0.9f);
    EXPECT_FLOAT_EQ(c.offset,     0.1f);
    EXPECT_EQ(c.phase,            45);
    EXPECT_EQ(c.duration,         600);
}

TEST_F(HapticParserTest, PeriodicDurationMsAlias) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"periodic","device":0,
            "params":{"duration":100,"duration_ms":1234}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::periodicCalls.size(), 1u);
    EXPECT_EQ(HapticStub::periodicCalls[0].duration, 1234);
}

// ═════════════════════════════════════════════════════════════════════════════
// Condition
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, ConditionEffectCallsQueueCondition) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"condition","device":0,
            "params":{"slot":1,"condition_type":1,
                      "right_sat":0.9,"left_sat":0.8,
                      "right_coeff":0.5,"left_coeff":0.4,
                      "deadband":0.1,"center":0.0,"duration":500}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::conditionCalls.size(), 1u);
    const auto& c = HapticStub::conditionCalls[0];
    EXPECT_EQ(c.slot,              1);
    EXPECT_EQ(c.type,              HapticConditionType::Damper);
    EXPECT_FLOAT_EQ(c.right_sat,   0.9f);
    EXPECT_FLOAT_EQ(c.left_sat,    0.8f);
    EXPECT_FLOAT_EQ(c.right_coeff, 0.5f);
    EXPECT_FLOAT_EQ(c.left_coeff,  0.4f);
    EXPECT_FLOAT_EQ(c.deadband,    0.1f);
    EXPECT_FLOAT_EQ(c.center,      0.0f);
    EXPECT_EQ(c.duration,          500);
}

TEST_F(HapticParserTest, ConditionDurationMsAlias) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"condition","device":0,
            "params":{"duration":1,"duration_ms":2000}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::conditionCalls.size(), 1u);
    EXPECT_EQ(HapticStub::conditionCalls[0].duration, 2000);
}

// ═════════════════════════════════════════════════════════════════════════════
// "data" container key (alternative to "params")
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, DataContainerKeyIsAccepted) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,
            "data":{"low":0.6,"high":0.6,"duration":150}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::rumbleCalls[0].low, 0.6f);
}

TEST_F(HapticParserTest, ParamsContainerTakesPriorityOverData) {
    // Both "params" and "data" present: "params" should win.
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,
            "params":{"low":0.9,"high":0.1,"duration":10},
            "data":{"low":0.1,"high":0.9,"duration":999}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::rumbleCalls[0].low, 0.9f);
}

TEST_F(HapticParserTest, AbsentContainerUsesDefaultValues) {
    // Neither "params" nor "data" — all fields should default.
    HapticParser::Parse(
        R"({"type":"haptic","effect":"constant","device":0})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::constantCalls[0].strength, 0.0f);
    EXPECT_EQ(HapticStub::constantCalls[0].duration, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Device ID
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, DeviceIdPassedThrough) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":3,
            "params":{"low":0.5,"high":0.5,"duration":100}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_EQ(HapticStub::rumbleCalls[0].device, 3);
}

TEST_F(HapticParserTest, MissingDeviceDefaultsToZero) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"constant","params":{"strength":0.5}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    EXPECT_EQ(HapticStub::constantCalls[0].device, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Slot passthrough — rumble, constant, periodic
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, RumbleSlotPassedThrough) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,
            "params":{"low":0.5,"high":0.5,"duration":100,"slot":3}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_EQ(HapticStub::rumbleCalls[0].slot, 3);
}

TEST_F(HapticParserTest, ConstantSlotPassedThrough) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"constant","device":0,
            "params":{"strength":0.5,"duration":100,"slot":2}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    EXPECT_EQ(HapticStub::constantCalls[0].slot, 2);
}

TEST_F(HapticParserTest, PeriodicSlotPassedThrough) {
    HapticParser::Parse(
        R"({"type":"haptic","effect":"periodic","device":0,
            "params":{"strength":1.0,"period":50,"magnitude":0.5,
                      "offset":0.0,"phase":0,"duration":200,"slot":5}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::periodicCalls.size(), 1u);
    EXPECT_EQ(HapticStub::periodicCalls[0].slot, 5);
}

TEST_F(HapticParserTest, SlotDefaultsToZeroWhenAbsent) {
    // Verify all three effect types default slot to 0 when the key is missing.
    HapticParser::Parse(
        R"({"type":"haptic","effect":"rumble","device":0,"params":{"low":0.1,"high":0.1,"duration":10}})",
        FakeMapper());
    HapticParser::Parse(
        R"({"type":"haptic","effect":"constant","device":0,"params":{"strength":0.1,"duration":10}})",
        FakeMapper());
    HapticParser::Parse(
        R"({"type":"haptic","effect":"periodic","device":0,"params":{"strength":0.1,"period":50,"magnitude":0.1,"offset":0.0,"phase":0,"duration":10}})",
        FakeMapper());

    ASSERT_EQ(HapticStub::rumbleCalls.size(),   1u);
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    ASSERT_EQ(HapticStub::periodicCalls.size(), 1u);

    EXPECT_EQ(HapticStub::rumbleCalls[0].slot,   0);
    EXPECT_EQ(HapticStub::constantCalls[0].slot, 0);
    EXPECT_EQ(HapticStub::periodicCalls[0].slot, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// "flight_stick" type string (new device type wired in PR)
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, TypeFlightStickRumbleIsAccepted) {
    HapticParser::Parse(
        R"({"type":"flight_stick","effect":"rumble","device":0,
            "params":{"low":0.6,"high":0.4,"duration":250}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::rumbleCalls[0].low,  0.6f);
    EXPECT_FLOAT_EQ(HapticStub::rumbleCalls[0].high, 0.4f);
}

TEST_F(HapticParserTest, TypeFlightStickConstantIsAccepted) {
    HapticParser::Parse(
        R"({"type":"flight_stick","effect":"constant","device":1,
            "params":{"strength":0.9,"duration":500}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::constantCalls[0].strength, 0.9f);
    EXPECT_EQ(HapticStub::constantCalls[0].device, 1);
}

TEST_F(HapticParserTest, TypeFlightStickConditionIsAccepted) {
    HapticParser::Parse(
        R"({"type":"flight_stick","effect":"condition","device":0,
            "params":{"condition_type":2,"right_sat":1.0,"left_sat":1.0,
                      "right_coeff":0.5,"left_coeff":0.5,
                      "deadband":0.1,"center":0.0,"duration":5000}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::conditionCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::conditionCalls[0].right_sat, 1.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
// AutoDetect — DetectedEffect kind inference
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, AutoDetectUnknownOnEmptyObject) {
    auto det = HapticParser::AutoDetect("{}");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Unknown);
}

TEST_F(HapticParserTest, AutoDetectUnknownOnBadJson) {
    auto det = HapticParser::AutoDetect("not json");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Unknown);
}

TEST_F(HapticParserTest, AutoDetectRumbleFromLowHighFields) {
    auto det = HapticParser::AutoDetect(
        R"({"device":2,"params":{"low":0.5,"high":0.3,"duration":200}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Rumble);
    EXPECT_EQ(det.device, 2);
    EXPECT_FLOAT_EQ(det.low,  0.5f);
    EXPECT_FLOAT_EQ(det.high, 0.3f);
    EXPECT_EQ(det.duration_ms, 200);
}

TEST_F(HapticParserTest, AutoDetectRumbleFromLargeMagnitudeFields) {
    auto det = HapticParser::AutoDetect(
        R"({"params":{"large_magnitude":0.8,"small_magnitude":0.2,"duration_ms":100}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Rumble);
    EXPECT_FLOAT_EQ(det.low,  0.8f);
    EXPECT_FLOAT_EQ(det.high, 0.2f);
}

TEST_F(HapticParserTest, AutoDetectConstantFromStrengthAlone) {
    auto det = HapticParser::AutoDetect(
        R"({"params":{"strength":0.75,"duration":400}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Constant);
    EXPECT_FLOAT_EQ(det.strength, 0.75f);
    EXPECT_EQ(det.duration_ms, 400);
}

TEST_F(HapticParserTest, AutoDetectPeriodicFromPeriodField) {
    auto det = HapticParser::AutoDetect(
        R"({"params":{"period":500,"magnitude":0.6,"offset":0.0,"phase":0,"duration":1000}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Periodic);
    EXPECT_EQ(det.period, 500);
    EXPECT_FLOAT_EQ(det.magnitude, 0.6f);
}

TEST_F(HapticParserTest, AutoDetectPeriodicFromMagnitudeAndOffset) {
    // No "period" key, but both "magnitude" and "offset" are present.
    auto det = HapticParser::AutoDetect(
        R"({"params":{"magnitude":0.4,"offset":0.1,"duration":800}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Periodic);
}

TEST_F(HapticParserTest, AutoDetectConditionFromRightSat) {
    auto det = HapticParser::AutoDetect(
        R"({"params":{"right_sat":1.0,"left_sat":1.0,
                      "right_coeff":0.5,"left_coeff":0.5,
                      "deadband":0.1,"center":0.0,"duration":5000}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Condition);
    EXPECT_FLOAT_EQ(det.right_sat, 1.0f);
    EXPECT_FLOAT_EQ(det.deadband,  0.1f);
}

TEST_F(HapticParserTest, AutoDetectConditionFromConditionTypeKey) {
    // condition_type alone is enough to identify the effect.
    // Uses index 2 (Inertia) to verify any valid index is accepted.
    auto det = HapticParser::AutoDetect(
        R"({"params":{"condition_type":2,"duration":3000}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Condition);
}

TEST_F(HapticParserTest, AutoDetectDualSenseTrigger) {
    auto det = HapticParser::AutoDetect(
        R"({"params":{"trigger":"right","effect_type":"feedback","duration":0}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::DualSenseTrigger);
    EXPECT_EQ(det.trigger,     "right");
    EXPECT_EQ(det.effect_type, "feedback");
}

// Condition must win over constant when both "strength" and sat fields co-exist.
TEST_F(HapticParserTest, AutoDetectConditionBeatsConstantWhenSatPresent) {
    auto det = HapticParser::AutoDetect(
        R"({"params":{"strength":0.5,"right_sat":1.0,"left_sat":1.0,"duration":1000}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Condition);
}

// Periodic must win over constant when "period" and "strength" co-exist.
TEST_F(HapticParserTest, AutoDetectPeriodicBeatsConstantWhenPeriodPresent) {
    auto det = HapticParser::AutoDetect(
        R"({"params":{"strength":0.5,"period":200,"magnitude":0.5,"duration":500}})");
    EXPECT_EQ(det.kind, DetectedEffect::Kind::Periodic);
}

// ═════════════════════════════════════════════════════════════════════════════
// Parse with "type":"auto" dispatches via AutoDetect
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(HapticParserTest, TypeAutoDispatchesRumble) {
    HapticParser::Parse(
        R"({"type":"auto","device":1,"params":{"low":0.4,"high":0.2,"duration":300}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_EQ(HapticStub::rumbleCalls[0].device, 1);
    EXPECT_FLOAT_EQ(HapticStub::rumbleCalls[0].low, 0.4f);
}

TEST_F(HapticParserTest, TypeAutoDispatchesConstant) {
    HapticParser::Parse(
        R"({"type":"auto","device":0,"params":{"strength":0.8,"duration":500}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::constantCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::constantCalls[0].strength, 0.8f);
}

TEST_F(HapticParserTest, TypeAutoDispatchesPeriodic) {
    HapticParser::Parse(
        R"({"type":"auto","device":0,"params":{"period":100,"magnitude":0.7,"offset":0,"duration":1000}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::periodicCalls.size(), 1u);
    EXPECT_EQ(HapticStub::periodicCalls[0].period, 100);
}

TEST_F(HapticParserTest, TypeAutoDispatchesCondition) {
    HapticParser::Parse(
        R"({"type":"auto","device":0,"params":{"right_sat":0.9,"left_sat":0.9,
            "right_coeff":0.5,"left_coeff":0.5,"deadband":0.05,"center":0.0,"duration":5000}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::conditionCalls.size(), 1u);
    EXPECT_FLOAT_EQ(HapticStub::conditionCalls[0].right_sat, 0.9f);
}

// "type" absent — treated the same as "auto"
TEST_F(HapticParserTest, MissingTypeAlsoAutoDetects) {
    HapticParser::Parse(
        R"({"device":3,"params":{"low":1.0,"high":0.5,"duration":150}})",
        FakeMapper());
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_EQ(HapticStub::rumbleCalls[0].device, 3);
}

// Unknown field set with "type":"auto" → no dispatch, no crash.
TEST_F(HapticParserTest, TypeAutoUnrecognisedFieldsProduceNoCalls) {
    HapticParser::Parse(
        R"({"type":"auto","device":0,"params":{"foo":1,"bar":2}})",
        FakeMapper());
    EXPECT_TRUE(HapticStub::rumbleCalls.empty());
    EXPECT_TRUE(HapticStub::constantCalls.empty());
    EXPECT_TRUE(HapticStub::periodicCalls.empty());
    EXPECT_TRUE(HapticStub::conditionCalls.empty());
}

// "effect" hint overrides field sniffing when both are present.
TEST_F(HapticParserTest, TypeAutoEffectHintOverridesFieldSniff) {
    // The params have both "strength" (constant hint) and explicit effect=rumble.
    HapticParser::Parse(
        R"({"type":"auto","effect":"rumble","device":0,
           "params":{"strength":0.5,"low":0.3,"high":0.2,"duration":200}})",
        FakeMapper());
    // Should produce a rumble call, not a constant call.
    ASSERT_EQ(HapticStub::rumbleCalls.size(), 1u);
    EXPECT_TRUE(HapticStub::constantCalls.empty());
}