// src/Devices/Wiimote/WiimoteDecoder.cpp
#include "WiimoteDecoder.h"
#include <cstring>

namespace InputBridge::Wiimote::Decode {

namespace {
// Standard CRC32 (reversed polynomial 0xEDB88320 - the zlib/PNG/Ethernet
// variant: init 0xFFFFFFFF, final XOR 0xFFFFFFFF), computed byte-at-a-time
// so this file stays free of any zlib/external dependency. Only used for
// Balance Board calibration verification below; not performance-sensitive
// (28 bytes, once per calibration load), so no lookup table.
uint32_t Crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}
} // namespace

CoreButtons Buttons(const uint8_t bb[2]) {
    CoreButtons s;
    s.left  = bb[0] & 0x01;
    s.right = bb[0] & 0x02;
    s.down  = bb[0] & 0x04;
    s.up    = bb[0] & 0x08;
    s.plus  = bb[0] & 0x10;
    s.two   = bb[1] & 0x01;
    s.one   = bb[1] & 0x02;
    s.b     = bb[1] & 0x04;
    s.a     = bb[1] & 0x08;
    s.minus = bb[1] & 0x10;
    s.home  = bb[1] & 0x80;
    return s;
}

// Bit-extraction for the accelerometer LSBs embedded in the button bytes.
// This is the extraction used consistently across the open-source Wiimote
// driver ecosystem (wiiuse, cwiid, WiimoteLib); WiiBrew's own bit table for
// this section is not fully column-aligned in its wiki markup, so treat this
// as the community-verified reference rather than a literal wiki transcription.
AccelState Accel(const uint8_t bb[2], const uint8_t aa[3]) {
    AccelState s;
    s.raw_x = (uint16_t(aa[0]) << 2) | ((bb[0] >> 5) & 0x03);
    s.raw_y = (uint16_t(aa[1]) << 2) | ((bb[1] >> 4) & 0x02);
    s.raw_z = (uint16_t(aa[2]) << 2) | ((bb[1] >> 5) & 0x02);

    // Nominal (uncalibrated) conversion: 0g ~= 512, 1g ~= 512 + 128 = 640,
    // per WiiBrew's accelerometer overview. For precise work, read the
    // per-device calibration block from EEPROM (WiimoteDevice::ReadAccelCalibration)
    // and use the real 0g/1g offsets instead of these nominal values.
    constexpr float kZeroG = 512.f, kOneGCounts = 128.f;
    s.g_x = (float(s.raw_x) - kZeroG) / kOneGCounts;
    s.g_y = (float(s.raw_y) - kZeroG) / kOneGCounts;
    s.g_z = (float(s.raw_z) - kZeroG) / kOneGCounts;
    return s;
}

IRState IRBasic(const uint8_t ir[10]) {
    IRState out{};
    auto decodePair = [](const uint8_t *p, IRDot &d0, IRDot &d1) {
        const uint16_t x0 = p[0] | (uint16_t(p[2] & 0x30) << 4);
        const uint16_t y0 = p[1] | (uint16_t(p[2] & 0xC0) << 2);
        const uint16_t x1 = p[3] | (uint16_t(p[2] & 0x03) << 8);
        const uint16_t y1 = p[4] | (uint16_t(p[2] & 0x0C) << 6);
        d0.visible = !(p[0] == 0xFF && p[1] == 0xFF);
        d1.visible = !(p[3] == 0xFF && p[4] == 0xFF);
        d0.x = x0; d0.y = y0;
        d1.x = x1; d1.y = y1;
    };
    decodePair(ir + 0, out[0], out[1]);
    decodePair(ir + 5, out[2], out[3]);
    return out;
}

IRState IRExtended(const uint8_t ir[12]) {
    IRState out{};
    // Per dot: byte0 = X low 8 bits, byte1 = Y low 8 bits, byte2 = Y-high
    // (bits 7:6), X-high (bits 5:4), size (bits 3:0) - same Y-before-X
    // nibble order as Basic Mode's pair-packing byte.
    // An empty slot reads all-1s across all 3 bytes (byte2 == 0xFF in
    // full), so the visibility check must cover the X/Y high-bit nibble
    // too, not just the size nibble - checking only bits 3:0 would
    // misclassify a real dot with size==15 as invisible.
    for (int i = 0; i < 4; ++i) {
        const uint8_t *p = ir + (i * 3);
        IRDot &d = out[i];
        d.x    = uint16_t(p[0]) | (uint16_t(p[2] & 0x30) << 4);
        d.y    = uint16_t(p[1]) | (uint16_t(p[2] & 0xC0) << 2);
        d.size = p[2] & 0x0F;
        d.visible = !(p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF);
    }
    return out;
}

NunchukState Nunchuk(const uint8_t *ext, size_t len) {
    NunchukState s;
    if (len < 6) return s; // disconnected/insufficient data
    s.connected = true;
    s.stick_x = ext[0];
    s.stick_y = ext[1];
    s.accel_x = (uint16_t(ext[2]) << 2) | ((ext[5] >> 2) & 0x03);
    s.accel_y = (uint16_t(ext[3]) << 2) | ((ext[5] >> 4) & 0x03);
    s.accel_z = (uint16_t(ext[4]) << 2) | ((ext[5] >> 6) & 0x03);
    s.button_c = !(ext[5] & 0x02); // 0 = pressed
    s.button_z = !(ext[5] & 0x01);
    return s;
}

ClassicControllerState Classic(const uint8_t *ext, size_t len, bool is_pro) {
    ClassicControllerState s;
    if (len < 6) return s;
    s.connected = true;
    s.is_pro = is_pro;

    const uint8_t b0 = ext[0], b1 = ext[1], b2 = ext[2], b3 = ext[3], b4 = ext[4], b5 = ext[5];

    s.left_x  = b0 & 0x3F;
    s.left_y  = b1 & 0x3F;
    s.right_x = uint16_t(((b0 >> 6) & 0x03) << 3 | ((b1 >> 6) & 0x03) << 1 | ((b2 >> 7) & 0x01));
    s.right_y = b2 & 0x1F;
    s.left_trigger  = uint16_t(((b2 >> 5) & 0x03) << 3 | ((b3 >> 5) & 0x07));
    s.right_trigger = b3 & 0x1F;

    // All buttons are active-low (0 = pressed) on the wire.
    s.dpad_right = !(b4 & 0x80);
    s.dpad_down  = !(b4 & 0x40);
    s.l          = !(b4 & 0x20);
    s.minus      = !(b4 & 0x10);
    s.home       = !(b4 & 0x08);
    s.plus       = !(b4 & 0x04);
    s.r          = !(b4 & 0x02);

    s.zl    = !(b5 & 0x80);
    s.b     = !(b5 & 0x40);
    s.y     = !(b5 & 0x20);
    s.a     = !(b5 & 0x10);
    s.x     = !(b5 & 0x08);
    s.zr    = !(b5 & 0x04);
    s.dpad_left = !(b5 & 0x02);
    s.dpad_up   = !(b5 & 0x01);

    return s;
}

GuitarHeroState GuitarFromClassic(const ClassicControllerState &cc, bool is_drums) {
    GuitarHeroState s;
    s.connected = cc.connected;
    s.is_drums = is_drums;
    s.fret_green  = cc.b;
    s.fret_red    = cc.a;
    s.fret_yellow = cc.y;
    s.fret_blue   = cc.x;
    s.fret_orange = cc.l;
    s.strum_up    = cc.dpad_up;
    s.strum_down  = cc.dpad_down;
    s.plus        = cc.plus;
    s.minus       = cc.minus;
    s.stick_x     = uint8_t(cc.left_x * 4); // rescale 0-63 -> ~0-252
    s.whammy_bar  = uint8_t(cc.right_trigger * 8); // rescale 0-31 -> ~0-248

    // Drum pad velocities aren't covered by this Classic-shaped decode;
    // GHWT Drums needs extra bytes/mode not modeled here yet.
    return s;
}

GuitarHeroState Guitar(const uint8_t *ext, size_t len, bool is_drums) {
    // GH guitars/drums stream fret/strum/whammy in the same 6-byte layout
    // as a stock Classic Controller (format 0x01), just with different
    // physical labels - same approach wiiuse's classic_ctrl.c takes.
    // WiiBrew notes the guitar actually advertises format 0x03 (8-bit,
    // 8-byte layout); if real hardware sends 8 bytes this mapping is wrong
    // and needs reworking against a real capture. Unverified on hardware.
    if (len < 6) return GuitarHeroState{};
    return GuitarFromClassic(Classic(ext, len, /*is_pro=*/false), is_drums);
}

BalanceBoardCalibration ParseBalanceBoardCalibration(const uint8_t block32[32],
                                                      const uint8_t ref_temp2[2]) {
    BalanceBoardCalibration c;

    // Checksum input, built in the exact (non-contiguous) order WiiBrew
    // documents: 0x24-0x3B (24 bytes), then 0x20-0x21 (2 bytes), then the
    // Reference Temperature bytes at 0x60-0x61 (2 bytes) - 28 bytes total.
    // block32[] is register-relative to 0x20, so 0x24-0x3B is block32[4..27]
    // and 0x20-0x21 is block32[0..1].
    uint8_t crc_input[28];
    std::memcpy(crc_input, block32 + 4, 24);
    crc_input[24] = block32[0];
    crc_input[25] = block32[1];
    crc_input[26] = ref_temp2[0];
    crc_input[27] = ref_temp2[1];

    const uint32_t computed = Crc32(crc_input, sizeof(crc_input));
    const uint32_t stored = (uint32_t(block32[0x3C - 0x20]) << 24) |
                             (uint32_t(block32[0x3D - 0x20]) << 16) |
                             (uint32_t(block32[0x3E - 0x20]) << 8) |
                             uint32_t(block32[0x3F - 0x20]);
    if (computed != stored) {
        // Corrupted/torn read: don't hand back numbers that look plausible
        // but aren't verified - leave `valid` false (all-zero arrays) so
        // BalanceBoard() skips kg conversion and the caller can decide to
        // keep whatever calibration it already had instead.
        return c;
    }

    auto be16 = [&](int off) -> uint16_t {
        return (uint16_t(block32[off]) << 8) | block32[off + 1];
    };
    // Layout starts at register 0x20 == block32[0]; offsets below are
    // (register - 0x20).
    // 0kg:  TR=0x24 BR=0x26 TL=0x28 BL=0x2A
    // 17kg: TR=0x2C BR=0x2E TL=0x30 BL=0x32
    // 34kg: TR=0x34 BR=0x36 TL=0x38 BL=0x3A
    c.kg0[0]  = be16(0x24 - 0x20); c.kg0[1]  = be16(0x26 - 0x20);
    c.kg0[2]  = be16(0x28 - 0x20); c.kg0[3]  = be16(0x2A - 0x20);
    c.kg17[0] = be16(0x2C - 0x20); c.kg17[1] = be16(0x2E - 0x20);
    c.kg17[2] = be16(0x30 - 0x20); c.kg17[3] = be16(0x32 - 0x20);
    c.kg34[0] = be16(0x34 - 0x20); c.kg34[1] = be16(0x36 - 0x20);
    c.kg34[2] = be16(0x38 - 0x20); c.kg34[3] = be16(0x3A - 0x20);
    c.valid = true;
    return c;
}

namespace {
// 3-point (0/17/34 kg) piecewise-linear interpolation, per WiiBrew: use the
// two nearest calibration points that bracket the reading, or the top two
// if the reading exceeds the highest calibration point (extrapolate).
float InterpolateWeight(uint16_t raw, uint16_t c0, uint16_t c17, uint16_t c34) {
    // Extrapolate below c0 the same way we extrapolate above c34, rather
    // than clamping to 0 - board flex can genuinely push a corner's raw
    // reading below its 0kg baseline (e.g. diagonal compression as weight
    // shifts elsewhere), and a small negative kg is a valid "slightly
    // unloaded" reading, not a fault. Callers can clamp for display.
    if (raw <= c17) {
        if (c17 == c0) return 0.f;
        return 17.f * (float(raw) - float(c0)) / float(c17 - c0);
    }
    if (c34 == c17) return 17.f;
    return 17.f + 17.f * (float(raw) - float(c17)) / float(c34 - c17);
}
} // namespace

BalanceBoardState BalanceBoard(const uint8_t ext[11], const BalanceBoardCalibration &cal) {
    BalanceBoardState s;
    s.connected = true;
    s.raw_top_right    = (uint16_t(ext[0]) << 8) | ext[1];
    s.raw_bottom_right = (uint16_t(ext[2]) << 8) | ext[3];
    s.raw_top_left     = (uint16_t(ext[4]) << 8) | ext[5];
    s.raw_bottom_left  = (uint16_t(ext[6]) << 8) | ext[7];
    s.temperature_raw  = ext[8];
    s.battery_raw       = ext[10];

    if (cal.valid) {
        s.kg_top_right    = InterpolateWeight(s.raw_top_right,    cal.kg0[0], cal.kg17[0], cal.kg34[0]);
        s.kg_bottom_right = InterpolateWeight(s.raw_bottom_right, cal.kg0[1], cal.kg17[1], cal.kg34[1]);
        s.kg_top_left     = InterpolateWeight(s.raw_top_left,     cal.kg0[2], cal.kg17[2], cal.kg34[2]);
        s.kg_bottom_left  = InterpolateWeight(s.raw_bottom_left,  cal.kg0[3], cal.kg17[3], cal.kg34[3]);
        s.kg_total = s.kg_top_right + s.kg_bottom_right + s.kg_top_left + s.kg_bottom_left;

        // Center of gravity: weighted average of corner positions, corners
        // at (+-1, +-1) with +x = right, +y = front (matches the Wii Fit
        // convention referenced on WiiBrew / Wikipedia).
        if (s.kg_total > 0.01f) {
            const float right = s.kg_top_right + s.kg_bottom_right;
            const float left  = s.kg_top_left  + s.kg_bottom_left;
            const float front = s.kg_top_right + s.kg_top_left;
            const float back  = s.kg_bottom_right + s.kg_bottom_left;
            s.cog_x = (right - left) / s.kg_total;
            s.cog_y = (front - back) / s.kg_total;
        }
    }
    return s;
}

MotionPlusState MotionPlus(const uint8_t *ext, size_t len) {
    // Byte layout (cross-checked against FreeIMU and Adafruit reference
    // implementations, since WiiBrew's own bit table is easy to mistranscribe):
    //   ext[0/1/2] = Yaw/Roll/Pitch low 8 bits
    //   ext[3/4/5] bits 7:2 = Yaw/Roll/Pitch high 6 bits
    //   ext[3] bit 1/0 = slow_yaw/slow_pitch; ext[4] bit 1 = slow_roll
    //   ext[4] bit 0 = extension_connected (passthrough device present)
    //   ext[5] bit 1 = report-type discriminator (1 = MotionPlus data) -
    //     WiimoteDevice checks this before calling here, not re-checked.
    MotionPlusState s;
    if (len < 6) return s; // disconnected/insufficient data
    s.connected = true;

    s.raw_yaw   = uint16_t(ext[0]) | (uint16_t(ext[3] & 0xFC) << 6);
    s.raw_roll  = uint16_t(ext[1]) | (uint16_t(ext[4] & 0xFC) << 6);
    s.raw_pitch = uint16_t(ext[2]) | (uint16_t(ext[5] & 0xFC) << 6);

    s.slow_yaw   = ext[3] & 0x02;
    s.slow_pitch = ext[3] & 0x01;
    s.slow_roll  = ext[4] & 0x02;

    s.extension_connected = ext[4] & 0x01;

    // Nominal conversion: zero-rate offset 8192 (14-bit centre; real
    // hardware idles closer to ~8063, so recalibrate at startup for
    // precision). Scale: ~13.768 counts/deg/s in "slow" range; "fast"
    // range covers 2000 vs 440 deg/s full-scale over the same code space.
    constexpr float kZero = 8192.f;
    constexpr float kSlowCountsPerDegS = 8192.f / 595.f; // ~13.768
    constexpr float kFastCountsPerDegS = kSlowCountsPerDegS * 440.f / 2000.f;
    auto toDegS = [&](uint16_t raw, bool slow) {
        const float countsPerDegS = slow ? kSlowCountsPerDegS : kFastCountsPerDegS;
        return (float(raw) - kZero) / countsPerDegS;
    };
    s.deg_s_yaw   = toDegS(s.raw_yaw,   s.slow_yaw);
    s.deg_s_pitch = toDegS(s.raw_pitch, s.slow_pitch);
    s.deg_s_roll  = toDegS(s.raw_roll,  s.slow_roll);

    return s;
}

NunchukState NunchukViaMotionPlus(const uint8_t *ext, size_t len) {
    // Per WiiBrew "Nunchuck pass-through mode": SX/SY untouched; each accel
    // axis loses its LSB (always 0 here) to make room for bookkeeping bits
    // relocated into ext[5]:
    //   bit 5/4 = AY/AX bit 1     bits 7:6 = AZ bits 2:1
    //   bit 3/2 = Button C/Z (active-low)   bits 1:0 = discriminator/reserved
    // AZ's top 7 bits stay in ext[4] bits 7:1; ext[4] bit 0 becomes
    // "extension connected".
    NunchukState s;
    if (len < 6) return s; // disconnected/insufficient data
    s.connected = true;
    s.stick_x = ext[0];
    s.stick_y = ext[1];
    s.accel_x = (uint16_t(ext[2]) << 2) | uint16_t(((ext[5] >> 4) & 0x01) << 1);
    s.accel_y = (uint16_t(ext[3]) << 2) | uint16_t(((ext[5] >> 5) & 0x01) << 1);
    s.accel_z = (uint16_t(ext[4] >> 1) << 3) | uint16_t(((ext[5] >> 6) & 0x03) << 1);
    s.button_c = !(ext[5] & 0x08);
    s.button_z = !(ext[5] & 0x04);
    return s;
}

ClassicControllerState ClassicViaMotionPlus(const uint8_t *ext, size_t len, bool is_pro) {
    // Per WiiBrew "Classic Controller pass-through mode": RX/RY/LT/RT and
    // all buttons except the D-pad sit at the same bits as Classic() above.
    // What differs: left stick X/Y each lose their LSB to make room for
    // BDU (moved to ext[0] bit 0) and BDL (moved to ext[1] bit 0); ext[4]
    // bit 0 becomes "extension connected"; ext[5] bits 1:0 become the
    // discriminator/reserved bits instead of dpad_left/up (reading them as
    // buttons, as plain Classic() would, misreports both as held).
    ClassicControllerState s;
    if (len < 6) return s;
    s.connected = true;
    s.is_pro = is_pro;

    const uint8_t b0 = ext[0], b1 = ext[1], b2 = ext[2], b3 = ext[3], b4 = ext[4], b5 = ext[5];

    s.left_x  = b0 & 0x3E; // LX<5:1>, bit 0 forced to 0 (stolen for BDU)
    s.left_y  = b1 & 0x3E; // LY<5:1>, bit 0 forced to 0 (stolen for BDL)
    s.right_x = uint16_t(((b0 >> 6) & 0x03) << 3 | ((b1 >> 6) & 0x03) << 1 | ((b2 >> 7) & 0x01));
    s.right_y = b2 & 0x1F;
    s.left_trigger  = uint16_t(((b2 >> 5) & 0x03) << 3 | ((b3 >> 5) & 0x07));
    s.right_trigger = b3 & 0x1F;

    s.dpad_right = !(b4 & 0x80);
    s.dpad_down  = !(b4 & 0x40);
    s.l          = !(b4 & 0x20);
    s.minus      = !(b4 & 0x10);
    s.home       = !(b4 & 0x08);
    s.plus       = !(b4 & 0x04);
    s.r          = !(b4 & 0x02);
    // b4 bit 0 here is "extension connected", not a button.

    s.zl    = !(b5 & 0x80);
    s.b     = !(b5 & 0x40);
    s.y     = !(b5 & 0x20);
    s.a     = !(b5 & 0x10);
    s.x     = !(b5 & 0x08);
    s.zr    = !(b5 & 0x04);
    // b5 bits 1:0 are the discriminator/reserved bits, not dpad_left/up.

    s.dpad_up   = !(b0 & 0x01);
    s.dpad_left = !(b1 & 0x01);

    return s;
}

} // namespace InputBridge::Wiimote::Decode
