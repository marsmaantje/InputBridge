// Tests for Devices/SpaceMouse/SpaceMouseProtocol.* - pure decoder and
// device-identification logic, no SDL / HID I/O.
#include <gtest/gtest.h>
#include "Devices/SpaceMouse/SpaceMouseProtocol.h"

using namespace InputBridge::SpaceMouse;

TEST(SpaceMouseDecode, TranslationReport) {
    State s;
    const uint8_t r[] = {0x01, 0x5E, 0x01, 0x9C, 0xFF, 0x00, 0x80}; // 350, -100, -32768
    EXPECT_EQ(DecodeReport(r, sizeof r, s), ReportKind::Translation);
    EXPECT_EQ(s.raw[0], 350);
    EXPECT_EQ(s.raw[1], -100);
    EXPECT_EQ(s.raw[2], -32768);
    EXPECT_EQ(s.raw[3], 0);
}

TEST(SpaceMouseDecode, RotationReportOnlyTouchesRotation) {
    State s; s.raw[0] = 7;
    const uint8_t r[] = {0x02, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00};
    EXPECT_EQ(DecodeReport(r, sizeof r, s), ReportKind::Rotation);
    EXPECT_EQ(s.raw[0], 7);
    EXPECT_EQ(s.raw[3], 1); EXPECT_EQ(s.raw[4], 2); EXPECT_EQ(s.raw[5], 3);
}

TEST(SpaceMouseDecode, CombinedReport) {
    State s;
    uint8_t r[13] = {0x01};
    for (int i = 0; i < 6; ++i) r[1 + i * 2] = uint8_t(i + 1);
    EXPECT_EQ(DecodeReport(r, sizeof r, s), ReportKind::Combined);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(s.raw[i], i + 1);
}

TEST(SpaceMouseDecode, ButtonsLsbFirstAndMultiByte) {
    State s;
    const uint8_t a[] = {0x03, 0x02};
    EXPECT_EQ(DecodeReport(a, sizeof a, s), ReportKind::Buttons);
    EXPECT_EQ(s.buttons, 0x2u);
    const uint8_t b[] = {0x03, 0x01, 0x00, 0x00, 0x80};
    DecodeReport(b, sizeof b, s);
    EXPECT_EQ(s.buttons, 0x80000001u);
    const uint8_t zero[] = {0x03, 0x00};
    DecodeReport(zero, sizeof zero, s);
    EXPECT_EQ(s.buttons, 0u);
}

TEST(SpaceMouseDecode, BatteryClamped) {
    State s;
    const uint8_t r[] = {0x17, 250};
    EXPECT_EQ(DecodeReport(r, sizeof r, s), ReportKind::Battery);
    EXPECT_EQ(s.battery_percent, 100);
}

TEST(SpaceMouseDecode, MalformedAndUnknownAreIgnored) {
    State s; s.raw[0] = 5; s.buttons = 3;
    const uint8_t shortT[] = {0x01, 0x01, 0x00};
    const uint8_t shortR[] = {0x02, 0x01};
    const uint8_t unknown[] = {0x42, 1, 2, 3, 4, 5, 6};
    EXPECT_EQ(DecodeReport(shortT, sizeof shortT, s), ReportKind::Ignored);
    EXPECT_EQ(DecodeReport(shortR, sizeof shortR, s), ReportKind::Ignored);
    EXPECT_EQ(DecodeReport(unknown, sizeof unknown, s), ReportKind::Ignored);
    EXPECT_EQ(DecodeReport(nullptr, 0, s), ReportKind::Ignored);
    EXPECT_EQ(s.raw[0], 5);
    EXPECT_EQ(s.buttons, 3u);
}

TEST(SpaceMouseNormalize, ClampsAndScales) {
    EXPECT_FLOAT_EQ(NormalizeAxis(0), 0.f);
    EXPECT_FLOAT_EQ(NormalizeAxis(350), 1.f);
    EXPECT_FLOAT_EQ(NormalizeAxis(-350), -1.f);
    EXPECT_FLOAT_EQ(NormalizeAxis(32000), 1.f);
    EXPECT_FLOAT_EQ(NormalizeAxis(-32768), -1.f);
    EXPECT_FLOAT_EQ(NormalizeAxis(175), 0.5f);
}

TEST(SpaceMouseIdentify, KnownModel) {
    auto m = IdentifyDevice(0x046D, 0xC626, 0, 0);
    ASSERT_TRUE(m.has_value());
    EXPECT_STREQ(m->name, "SpaceNavigator");
    EXPECT_EQ(m->buttons, 2);
}

TEST(SpaceMouseIdentify, NuLOOQNavigatorRecognised) {
    auto m = IdentifyDevice(0x046D, 0xC640, 0x01, 0x08);
    ASSERT_TRUE(m.has_value());
    EXPECT_STREQ(m->name, "NuLOOQ navigator");
}

TEST(SpaceMouseIdentify, KnownModelNonMotionInterfaceRejected) {
    EXPECT_FALSE(IdentifyDevice(0x256F, 0xC62E, 0xFF00, 0x01).has_value());
    EXPECT_TRUE(IdentifyDevice(0x256F, 0xC62E, 0x01, 0x08).has_value());
}

TEST(SpaceMouseIdentify, UnknownPidOnlyViaMultiAxisUsageOnVendorId) {
    auto m = IdentifyDevice(0x256F, 0xABCD, 0x01, 0x08);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->product_id, 0xABCD);
    EXPECT_EQ(m->buttons, kMaxButtons);
    EXPECT_FALSE(IdentifyDevice(0x256F, 0xABCD, 0, 0).has_value());
    EXPECT_FALSE(IdentifyDevice(0x256F, 0xABCD, 0x01, 0x02).has_value()); // a CadMouse
}

TEST(SpaceMouseIdentify, NeverClaimsOtherLogitechDevices) {
    EXPECT_FALSE(IdentifyDevice(0x046D, 0xC52B, 0x01, 0x08).has_value());
    EXPECT_FALSE(IdentifyDevice(0x046D, 0xC52B, 0, 0).has_value());
    EXPECT_FALSE(IdentifyDevice(0x057E, 0x0306, 0x01, 0x08).has_value());
}

TEST(SpaceMouseButtonName, TwoButtonModelsNamed) {
    auto m = *IdentifyDevice(0x046D, 0xC626, 0, 0);
    EXPECT_EQ(ButtonName(m, 0), "Left Button");
    EXPECT_EQ(ButtonName(m, 1), "Right Button");
    auto p = *IdentifyDevice(0x046D, 0xC62B, 0, 0);
    EXPECT_EQ(ButtonName(p, 3), "Button 3");
}

TEST(SpaceMouseModelLookup, FindModelByNameAcceptsStdStringAndLiteral) {
    const std::string name = "SpaceNavigator";
    const ModelInfo *m = FindModelByName(name);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->product_id, 0xC626);
    EXPECT_NE(FindModelByName("SpaceMouse Pro"), nullptr);
    EXPECT_EQ(FindModelByName("Nope"), nullptr);
}
