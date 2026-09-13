#include <gtest/gtest.h>
#include "Devices/Wiimote/WiimoteDecoder.h"
#include "Devices/Wiimote/WiimoteProtocol.h"

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

TEST(WiimoteDecoder, IRExtendedFieldsAreIndependent) {
    // Per WiiBrew's Extended Mode byte2 table: bits 7:6 = Y high, bits 5:4 =
    // X high, bits 3:0 = size (Y comes before X in byte2, opposite order
    // from what you'd guess by analogy with byte0/byte1). Dot 0: X low=0x11,
    // Y low=0x22, byte2 = Yhi(10) Xhi(01) size(1010) -> Y=0x222, X=0x111,
    // size=0xA. Distinct X/Y high bits (10 vs 01) make a swapped-axis bug
    // fail loudly. Dot 1 left zeroed/untouched to check no cross-talk
    // between dot slots.
    uint8_t byte2 = 0b10'01'1010;
    uint8_t ir[12] = {0x11, 0x22, byte2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto dots = Decode::IRExtended(ir);
    EXPECT_EQ(dots[0].x, 0x111);
    EXPECT_EQ(dots[0].y, 0x222);
    EXPECT_EQ(dots[0].size, 0xA);
    EXPECT_TRUE(dots[0].visible);
}

TEST(WiimoteDecoder, IRExtendedEmptySlotIsInvisible) {
    uint8_t ir[12] = {0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto dots = Decode::IRExtended(ir);
    EXPECT_FALSE(dots[0].visible);
}

TEST(WiimoteDecoder, IRExtendedMaxSize) {
    // size = 0xF (max, not the "empty slot" sentinel unless X/Y are ALSO
    // 0xFF) - a real large/bright dot near the edge of a 10-bit axis.
    uint8_t ir[12] = {0xFF, 0x00, 0x0F, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto dots = Decode::IRExtended(ir);
    EXPECT_TRUE(dots[0].visible);
    EXPECT_EQ(dots[0].size, 0xF);
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
// Extension encryption ("old way" init)
// ═══════════════════════════════════════════════════════════════════════════

// 0x17 is a fixed point of the transform: (0x17 ^ 0x17) + 0x17 == 0x17.
TEST(WiimoteDecoder, DecryptFixedPoint) {
    EXPECT_EQ(DecryptExtensionByte(0x17), 0x17);
}

TEST(WiimoteDecoder, DecryptIsEncryptInverse) {
    // Encrypting per WiiBrew is the same shape run the other direction:
    // encrypted = (plain - 0x17) ^ 0x17. Round-trip every byte value and
    // confirm decrypt undoes it.
    for (int plain = 0; plain <= 0xFF; ++plain) {
        const uint8_t encrypted = static_cast<uint8_t>((plain - 0x17) ^ 0x17);
        EXPECT_EQ(DecryptExtensionByte(encrypted), static_cast<uint8_t>(plain));
    }
}

TEST(WiimoteDecoder, DecryptExtensionBytesTransformsWholeBuffer) {
    uint8_t buf[6] = {0x2F, 0x2F, 0x2F, 0x2F, 0x2F, 0x2F};
    DecryptExtensionBytes(buf, 6);
    for (uint8_t b : buf) EXPECT_EQ(b, DecryptExtensionByte(0x2F));
}

TEST(WiimoteDecoder, EncryptedNunchukIdClassifiesAfterDecrypt) {
    // A genuine Nunchuk ID (0x00 0x00 0xA4 0x20 0x00 0x00), each byte
    // encrypted per WiiBrew's transform - this is what a wireless/
    // third-party Nunchuk that never disables encryption would return from
    // Registers::ExtensionId even after the "new way" init, and what
    // InitExtension()'s "old way" fallback is meant to recover from.
    ExtensionId6 id{{0xFE, 0xFE, 0x9A, 0x1E, 0xFE, 0xFE}};
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::Unknown); // raw bytes: not recognizable
    DecryptExtensionBytes(id.bytes.data(), id.bytes.size());
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::Nunchuk); // decrypted: recognizable
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
    // (4)A4003C: A9 06 B4 F0  <- WiiBrew's documented checksum for this exact sample
    block[0x3C - 0x20] = 0xA9; block[0x3D - 0x20] = 0x06;
    block[0x3E - 0x20] = 0xB4; block[0x3F - 0x20] = 0xF0;
    // (4)A40060: 19 01  <- Reference Temperature + unknown byte, folded into the CRC
    const uint8_t ref_temp[2] = {0x19, 0x01};

    auto cal = Decode::ParseBalanceBoardCalibration(block, ref_temp);
    ASSERT_TRUE(cal.valid);
    EXPECT_EQ(cal.kg0[0],  0x07BC); // TR 0kg
    EXPECT_EQ(cal.kg17[0], 0x0E6E); // TR 17kg
    EXPECT_EQ(cal.kg34[0], 0x152E); // TR 34kg
    EXPECT_EQ(cal.kg0[3],  0x4652); // BL 0kg (last pair in the 0kg row)
}

// A corrupted read (calibration bytes fine, but the wrong reference-
// temperature byte fed in - simulating a torn/partial read) must not
// silently hand back the otherwise-plausible-looking calibration values.
TEST(WiimoteDecoder, BalanceBoardCalibrationRejectsBadChecksum) {
    uint8_t block[32] = {};
    block[0] = 0x01; block[1] = 0x69; block[2] = 0x00; block[3] = 0x00;
    const uint8_t kg0[8]  = {0x07, 0xBC, 0x11, 0x8B, 0x06, 0xBA, 0x46, 0x52};
    const uint8_t kg17[8] = {0x0E, 0x6E, 0x18, 0x79, 0x0D, 0x5D, 0x4D, 0x4C};
    const uint8_t kg34[8] = {0x15, 0x2E, 0x1F, 0x71, 0x14, 0x07, 0x54, 0x51};
    memcpy(block + (0x24 - 0x20), kg0, 8);
    memcpy(block + (0x2C - 0x20), kg17, 8);
    memcpy(block + (0x34 - 0x20), kg34, 8);
    block[0x3C - 0x20] = 0xA9; block[0x3D - 0x20] = 0x06;
    block[0x3E - 0x20] = 0xB4; block[0x3F - 0x20] = 0xF0;
    // Wrong reference-temperature bytes (should be 19 01) -> checksum won't match.
    const uint8_t bad_ref_temp[2] = {0x00, 0x00};

    auto cal = Decode::ParseBalanceBoardCalibration(block, bad_ref_temp);
    EXPECT_FALSE(cal.valid);
    EXPECT_EQ(cal.kg0[0], 0); // corrupted read must not leak the parsed-but-unverified values
}

// Register value from WiiBrew's "Wii Initialisation Sequence" trace: the
// "Write f1: ..." lines are, at PC-interface addressing (0xa40000 + the
// listed byte), register 0xa400f1. WiimoteDevice::LoadBalanceBoardCalibration()
// writes to this register to reproduce the wake sequence the real Wii
// performs before trusting all 4 weight sensors - pin the constant here so
// a future refactor can't silently drift it away from the documented
// address without a test noticing.
TEST(WiimoteDecoder, BalanceBoardWakeRegisterMatchesWiiBrewTrace) {
    EXPECT_EQ(Registers::BalanceBoardWake, 0xA400F1u);
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

// ═══════════════════════════════════════════════════════════════════════════
// Extension classification - Wii Motion Plus
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, ClassifyMotionPlusStandalone) {
    ExtensionId6 id{{0x00, 0x00, 0xA4, 0x20, 0x00, 0x05}};
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::MotionPlus);
    EXPECT_EQ(ClassifyMotionPlusPassthrough(id), MotionPlusPassthrough::None);
}

TEST(WiimoteDecoder, ClassifyMotionPlusNunchukPassthrough) {
    ExtensionId6 id{{0x00, 0x00, 0xA4, 0x20, 0x04, 0x05}};
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::MotionPlus);
    EXPECT_EQ(ClassifyMotionPlusPassthrough(id), MotionPlusPassthrough::Nunchuk);
}

TEST(WiimoteDecoder, ClassifyMotionPlusClassicPassthrough) {
    ExtensionId6 id{{0x00, 0x00, 0xA4, 0x20, 0x05, 0x05}};
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::MotionPlus);
    EXPECT_EQ(ClassifyMotionPlusPassthrough(id), MotionPlusPassthrough::Classic);
}

TEST(WiimoteDecoder, ClassifyBalanceBoardStillWorksAlongsideMotionPlus) {
    // Regression guard: adding MotionPlus's 0x0005/0x0405/0x0505/0x0705
    // cases must not shadow the pre-existing Balance Board (0x0402) case.
    ExtensionId6 id{{0x00, 0x00, 0xA4, 0x20, 0x04, 0x02}};
    EXPECT_EQ(ClassifyExtension(id), ExtensionType::BalanceBoard);
}

// ═══════════════════════════════════════════════════════════════════════════
// Wii Motion Plus gyro decode
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, MotionPlusZeroRateAtNominalCenter) {
    // raw = 8192 (0x2000) on all three axes, "fast"/normal precision range,
    // no passthrough extension attached.
    uint8_t ext[6] = {
        0x00,       // yaw low
        0x00,       // roll low
        0x00,       // pitch low
        0x80,       // yaw high bits (0x2000 >> 6 = 0x80), slow_yaw=0, slow_pitch=0
        0x80,       // roll high bits, extension_connected=0
        0x80,       // pitch high bits, slow_roll=0
    };
    auto mp = Decode::MotionPlus(ext, 6);
    ASSERT_TRUE(mp.connected);
    EXPECT_EQ(mp.raw_yaw, 8192);
    EXPECT_EQ(mp.raw_pitch, 8192);
    EXPECT_EQ(mp.raw_roll, 8192);
    EXPECT_NEAR(mp.deg_s_yaw, 0.f, 0.001f);
    EXPECT_NEAR(mp.deg_s_pitch, 0.f, 0.001f);
    EXPECT_NEAR(mp.deg_s_roll, 0.f, 0.001f);
    EXPECT_FALSE(mp.slow_yaw);
    EXPECT_FALSE(mp.slow_pitch);
    EXPECT_FALSE(mp.slow_roll);
    EXPECT_FALSE(mp.extension_connected);
}

TEST(WiimoteDecoder, MotionPlusExtensionConnectedBitAndSlowFlags) {
    uint8_t ext[6] = {0x00, 0x00, 0x00, 0x83, 0x83, 0x80};
    // ext[3] bit0 (slow_yaw)=1, bit1 (slow_pitch)=1              -> 0x83
    // ext[4] bit0 (extension_connected)=1, bit1 (slow_roll)=1    -> 0x83
    auto mp = Decode::MotionPlus(ext, 6);
    EXPECT_TRUE(mp.slow_yaw);
    EXPECT_TRUE(mp.slow_pitch);
    EXPECT_TRUE(mp.slow_roll);
    EXPECT_TRUE(mp.extension_connected);
}

TEST(WiimoteDecoder, MotionPlusDisconnectedWithInsufficientData) {
    auto mp = Decode::MotionPlus(nullptr, 0);
    EXPECT_FALSE(mp.connected);
}

// ═══════════════════════════════════════════════════════════════════════════
// Nunchuk / Classic Controller passthrough decode (Wii Motion Plus active)
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteDecoder, NunchukViaMotionPlusSticksUnaffected) {
    // SX/SY pass through untouched regardless of passthrough re-encoding.
    uint8_t ext[6] = {0x7F, 0x81, 0x00, 0x00, 0x00, 0x00};
    auto n = Decode::NunchukViaMotionPlus(ext, 6);
    ASSERT_TRUE(n.connected);
    EXPECT_EQ(n.stick_x, 0x7F);
    EXPECT_EQ(n.stick_y, 0x81);
}

TEST(WiimoteDecoder, NunchukViaMotionPlusAccelReconstructsWithLsbZero) {
    // AX high byte = 0xAA, AX's relocated bit1 set (ext[5] bit4) ->
    // raw_x = (0xAA << 2) | (1 << 1) = 0x2A8 | 0x2 = 0x2AA, LSB forced 0.
    // Likewise AY (ext[3]=0x55, ext[5] bit5 set) and AZ (ext[4]>>1 top 7
    // bits = 0x7F i.e. ext[4]=0xFF, ext[5] bits7:6 set -> AZ<2:1>=3).
    uint8_t ext[6] = {
        0x00, 0x00,       // SX, SY (irrelevant here)
        0xAA,              // AX<9:2>
        0x55,              // AY<9:2>
        0xFF,              // AZ<9:3> in bits7:1, bit0 = extension_connected
        uint8_t(0xC0 | 0x20 | 0x10), // AZ<2:1>=11 (bits7:6), AY<1>=1 (bit5), AX<1>=1 (bit4)
    };
    auto n = Decode::NunchukViaMotionPlus(ext, 6);
    EXPECT_EQ(n.accel_x, (uint16_t(0xAA) << 2) | 0x02);
    EXPECT_EQ(n.accel_y, (uint16_t(0x55) << 2) | 0x02);
    EXPECT_EQ(n.accel_z, (uint16_t(0xFF >> 1) << 3) | 0x06);
    EXPECT_EQ(n.accel_x & 0x01, 0); // LSB always lost in passthrough
    EXPECT_EQ(n.accel_y & 0x01, 0);
    EXPECT_EQ(n.accel_z & 0x01, 0);
}

TEST(WiimoteDecoder, NunchukViaMotionPlusButtonsAtRelocatedBits) {
    // C = ext[5] bit3, Z = ext[5] bit2 (moved from the non-passthrough
    // format's bit1/bit0), both active-low.
    uint8_t ext[6] = {0, 0, 0, 0, 0, uint8_t(0xFF & ~0x08)}; // C pressed, Z released
    auto n = Decode::NunchukViaMotionPlus(ext, 6);
    EXPECT_TRUE(n.button_c);
    EXPECT_FALSE(n.button_z);
}

TEST(WiimoteDecoder, NunchukViaMotionPlusDisconnectedWithInsufficientData) {
    auto n = Decode::NunchukViaMotionPlus(nullptr, 0);
    EXPECT_FALSE(n.connected);
}

TEST(WiimoteDecoder, ClassicViaMotionPlusSticksLoseLsbAndDpadMoves) {
    // LX = 0x3F (all 6 bits set) with BDU pressed -> byte0 = 0x3F & ~0x01
    // active-low means BDU pressed = bit0 clear, so byte0 = 0x3E.
    // Reconstructed left_x should read back 0x3E (LSB forced 0), not 0x3F.
    uint8_t ext[6] = {
        0x3E, // LX<5:1>=0x1F, bit0=0 -> BDU pressed
        0x3E, // LY<5:1>=0x1F, bit0=0 -> BDL pressed
        0x00, 0x00,
        0xFF, // all of BDR/BDD/BLT/-/H/+/RT released (active-low, all 1)
        0xFF, // all of ZL/B/Y/A/X/ZR released
    };
    auto c = Decode::ClassicViaMotionPlus(ext, 6, false);
    ASSERT_TRUE(c.connected);
    EXPECT_EQ(c.left_x, 0x3E);
    EXPECT_EQ(c.left_y, 0x3E);
    EXPECT_TRUE(c.dpad_up);   // BDU, from ext[0] bit0
    EXPECT_TRUE(c.dpad_left); // BDL, from ext[1] bit0
}

TEST(WiimoteDecoder, ClassicViaMotionPlusReservedBitsDontLookLikeDpad) {
    // ext[5] bits1:0 are the MotionPlus discriminator/reserved bits (always
    // 0 in a real passthrough report) - the plain Classic() decoder would
    // misread these as dpad_left/dpad_up permanently pressed (active-low);
    // the *ViaMotionPlus decoder must not.
    uint8_t ext[6] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}; // BDU/BDL released via bit0=1 below
    ext[0] = 0x01; // BDU bit set = released
    ext[1] = 0x01; // BDL bit set = released
    auto c = Decode::ClassicViaMotionPlus(ext, 6, false);
    EXPECT_FALSE(c.dpad_up);
    EXPECT_FALSE(c.dpad_left);
}

TEST(WiimoteDecoder, ClassicViaMotionPlusFaceButtonsUnaffected) {
    // A = byte5 bit4, same position as the non-passthrough format.
    uint8_t ext[6] = {0x01, 0x01, 0x00, 0x00, 0xFF, uint8_t(0xFF & ~0x10)};
    auto c = Decode::ClassicViaMotionPlus(ext, 6, false);
    EXPECT_TRUE(c.a);
    EXPECT_FALSE(c.b);
}

TEST(WiimoteDecoder, ClassicViaMotionPlusDisconnectedWithInsufficientData) {
    auto c = Decode::ClassicViaMotionPlus(nullptr, 0, false);
    EXPECT_FALSE(c.connected);
}

TEST(WiimoteDecoder, GuitarFromClassicMatchesGuitarMapping) {
    // GuitarFromClassic() should reproduce exactly what Guitar() computes
    // when fed the same (non-passthrough) Classic-shaped bytes, since
    // Guitar() is now implemented in terms of it.
    uint8_t ext[6] = {0x3F, 0x00, 0x00, uint8_t(0x1F), 0xFF, uint8_t(0xFF & ~0x40)}; // B (green fret) pressed
    auto direct = Decode::Guitar(ext, 6, false);
    auto viaHelper = Decode::GuitarFromClassic(Decode::Classic(ext, 6, false), false);
    EXPECT_EQ(direct.fret_green, viaHelper.fret_green);
    EXPECT_EQ(direct.stick_x, viaHelper.stick_x);
    EXPECT_EQ(direct.whammy_bar, viaHelper.whammy_bar);
    EXPECT_TRUE(viaHelper.fret_green);
}
