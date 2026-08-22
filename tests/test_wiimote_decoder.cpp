#include <gtest/gtest.h>
#include "Devices/Wiimote/WiimoteDecoder.h"

using namespace InputBridge::Wiimote;

// ═══════════════════════════════════════════════════════════════════════════
// Core buttons
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, NoButtonsPressed) {
    uint8_t bb[2] = {0x00, 0x00};
    auto b = Decode::Buttons(bb);
    EXPECT_FALSE(b.a); EXPECT_FALSE(b.b); EXPECT_FALSE(b.home);
    EXPECT_FALSE(b.left); EXPECT_FALSE(b.plus);
}

TEST(WiimoteDecoder, AAndHomePressed) {
    // A = byte1 bit3 (0x08), Home = byte1 bit7 (0x80)
    uint8_t bb[2] = {0x00, 0x88};
    auto b = Decode::Buttons(bb);
    EXPECT_TRUE(b.a);
    EXPECT_TRUE(b.home);
    EXPECT_FALSE(b.b);
}

TEST(WiimoteDecoder, DpadAndPlus) {
    // Left=0x01, Right=0x02, Down=0x04, Up=0x08, Plus=0x10
    uint8_t bb[2] = {0x1F, 0x00};
    auto b = Decode::Buttons(bb);
    EXPECT_TRUE(b.left); EXPECT_TRUE(b.right); EXPECT_TRUE(b.down);
    EXPECT_TRUE(b.up);   EXPECT_TRUE(b.plus);
}

// ═══════════════════════════════════════════════════════════════════════════
// IR basic mode.
//
// NOTE: WiiBrew's worked example for this 5-byte packed format (in the
// EEPROM IR-calibration section) round-tripped inconsistently against its
// own bit table when checked by hand for this file - X came out right, Y did
// not. Rather than bake in a possibly-mistranscribed "known good" answer,
// these tests check field independence (each of the 4 values in a pair is
// distinct and lands in the right dot/axis) using bit patterns chosen to
// make a swapped X/Y or swapped-object bug fail loudly. Cross-check the
// packing in WiimoteDecoder.cpp against a live capture (e.g. via the debug
// log panel) before relying on exact IR coordinates in production.
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, IRBasicFieldsAreIndependent) {
    // X1 low=0x11, high bits(byte2 bits5:4)=01 -> X1=0x111
    // Y1 low=0x22, high bits(byte2 bits7:6)=10 -> Y1=0x222
    // X2 low=0x33, high bits(byte2 bits1:0)=11 -> X2=0x333
    // Y2 low=0x44, high bits(byte2 bits3:2)=00 -> Y2=0x044
    uint8_t byte2 = 0b10'01'00'11; // Y1=10 X1=01 Y2=00 X2=11
    uint8_t ir[10] = {0x11, 0x22, byte2, 0x33, 0x44, 0, 0, 0, 0, 0};
    auto dots = Decode::IRBasic(ir);
    EXPECT_EQ(dots[0].x, 0x111);
    EXPECT_EQ(dots[0].y, 0x222);
    EXPECT_EQ(dots[1].x, 0x333);
    EXPECT_EQ(dots[1].y, 0x044);
    EXPECT_NE(dots[0].x, dots[0].y); // sanity: not accidentally aliased
}

TEST(WiimoteDecoder, IRBasicEmptySlotIsInvisible) {
    uint8_t ir[10] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto dots = Decode::IRBasic(ir);
    EXPECT_FALSE(dots[0].visible);
}

// ═══════════════════════════════════════════════════════════════════════════
// Extension identification
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, ClassifiesNunchuk) {
    ExtensionId6 id{{0x00, 0x00, 0xA4, 0x20, 0x00, 0x00}};
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::Nunchuk);
}

TEST(WiimoteDecoder, ClassifiesClassicControllerVsPro) {
    ExtensionId6 stock{{0x00, 0x00, 0xA4, 0x20, 0x01, 0x01}};
    ExtensionId6 pro  {{0x01, 0x00, 0xA4, 0x20, 0x01, 0x01}};
    EXPECT_EQ(ClassifyExtension(stock), ExtensionType::ClassicController);
    EXPECT_EQ(ClassifyExtension(pro),   ExtensionType::ClassicControllerPro);
}

TEST(WiimoteDecoder, ClassifiesBalanceBoard) {
    ExtensionId6 id{{0x00, 0x00, 0xA4, 0x20, 0x04, 0x02}};
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::BalanceBoard);
}

TEST(WiimoteDecoder, ClassifiesGuitarVsDrums) {
    ExtensionId6 guitar{{0x00, 0x00, 0xA4, 0x20, 0x01, 0x03}};
    ExtensionId6 drums {{0x01, 0x00, 0xA4, 0x20, 0x01, 0x03}};
    EXPECT_EQ(ClassifyExtension(guitar), ExtensionType::GuitarHeroGuitar);
    EXPECT_EQ(ClassifyExtension(drums),  ExtensionType::GuitarHeroDrums);
}

// ═══════════════════════════════════════════════════════════════════════════
// Balance Board - "US Board" sample calibration from WiiBrew, spot-checked
// with a synthetic mid-scale reading per sensor.
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, BalanceBoardCalibrationParsesUSBoardSample) {
    uint8_t block[32] = {};
    // (4)A40020: 01 69 00 00
    block[0] = 0x01; block[1] = 0x69; block[2] = 0x00; block[3] = 0x00;
    // (4)A40024: 07 BC 11 8B  06 BA 46 52   <- 0kg: TR BR TL BL
    const uint8_t kg0[8]  = {0x07, 0xBC, 0x11, 0x8B, 0x06, 0xBA, 0x46, 0x52};
    // (4)A4002C: 0E 6E 18 79  0D 5D 4D 4C   <- 17kg
    const uint8_t kg17[8] = {0x0E, 0x6E, 0x18, 0x79, 0x0D, 0x5D, 0x4D, 0x4C};
    // (4)A40034: 15 2E 1F 71  14 07 54 51   <- 34kg
    const uint8_t kg34[8] = {0x15, 0x2E, 0x1F, 0x71, 0x14, 0x07, 0x54, 0x51};
    memcpy(block + (0x24 - 0x20), kg0, 8);
    memcpy(block + (0x2C - 0x20), kg17, 8);
    memcpy(block + (0x34 - 0x20), kg34, 8);

    auto cal = Decode::ParseBalanceBoardCalibration(block);
    ASSERT_TRUE(cal.valid);
    EXPECT_EQ(cal.kg0[0],  0x07BC); // TR 0kg
    EXPECT_EQ(cal.kg17[0], 0x0E6E); // TR 17kg
    EXPECT_EQ(cal.kg34[0], 0x152E); // TR 34kg
    EXPECT_EQ(cal.kg0[3],  0x4652); // BL 0kg (last pair in the 0kg row)
}

TEST(WiimoteDecoder, BalanceBoardInterpolatesWeightAtCalibrationPoints) {
    BalanceBoardCalibration cal;
    cal.valid = true;
    for (int i = 0; i < 4; ++i) { cal.kg0[i] = 1000; cal.kg17[i] = 2000; cal.kg34[i] = 3000; }

    uint8_t ext[11] = {};
    // TR = 2000 raw (== the 17kg calibration point exactly)
    ext[0] = 0x07; ext[1] = 0xD0; // 2000
    ext[2] = 0x03; ext[3] = 0xE8; // BR = 1000 -> should read ~0kg
    ext[4] = 0x0B; ext[5] = 0xB8; // TL = 3000 -> should read ~34kg
    ext[6] = 0x03; ext[7] = 0xE8; // BL = 1000 -> ~0kg
    ext[8] = 0x1A; ext[9] = 0x00; ext[10] = 0x90;

    auto bb = Decode::BalanceBoard(ext, cal);
    EXPECT_NEAR(bb.kg_top_right, 17.f, 0.01f);
    EXPECT_NEAR(bb.kg_bottom_right, 0.f, 0.01f);
    EXPECT_NEAR(bb.kg_top_left, 34.f, 0.01f);
    EXPECT_NEAR(bb.kg_bottom_left, 0.f, 0.01f);
    EXPECT_NEAR(bb.kg_total, 51.f, 0.05f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Nunchuk
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, NunchukCenterStickBothButtonsReleased) {
    // Center stick ~128, accel ~mid, both buttons released (bits set = 1).
    uint8_t ext[6] = {128, 128, 0x80, 0x80, 0x80, 0x03};
    auto n = Decode::Nunchuk(ext, 6);
    ASSERT_TRUE(n.connected);
    EXPECT_EQ(n.stick_x, 128);
    EXPECT_EQ(n.stick_y, 128);
    EXPECT_FALSE(n.button_c);
    EXPECT_FALSE(n.button_z);
}

TEST(WiimoteDecoder, NunchukBothButtonsPressed) {
    uint8_t ext[6] = {128, 128, 0x80, 0x80, 0x80, 0x00}; // bits 0,1 clear = both pressed
    auto n = Decode::Nunchuk(ext, 6);
    EXPECT_TRUE(n.button_c);
    EXPECT_TRUE(n.button_z);
}

TEST(WiimoteDecoder, NunchukDisconnectedWithInsufficientData) {
    auto n = Decode::Nunchuk(nullptr, 0);
    EXPECT_FALSE(n.connected);
}

// ═══════════════════════════════════════════════════════════════════════════
// Classic Controller - all-released should decode to no buttons active.
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, ClassicControllerAllButtonsReleased) {
    uint8_t ext[6] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    auto c = Decode::Classic(ext, 6, false);
    ASSERT_TRUE(c.connected);
    EXPECT_FALSE(c.a); EXPECT_FALSE(c.b); EXPECT_FALSE(c.home);
    EXPECT_FALSE(c.dpad_up); EXPECT_FALSE(c.dpad_down);
}

TEST(WiimoteDecoder, ClassicControllerAPressed) {
    // A = byte5 bit4 (0x10), active low -> clear that bit, others stay set.
    uint8_t ext[6] = {0x00, 0x00, 0x00, 0x00, 0xFF, uint8_t(0xFF & ~0x10)};
    auto c = Decode::Classic(ext, 6, false);
    EXPECT_TRUE(c.a);
    EXPECT_FALSE(c.b);
}
