// src/Devices/Wiimote/WiimoteDecoder.cpp
#include "WiimoteDecoder.h"
#include <cstring>

namespace InputBridge::Wiimote::Decode {

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
    s.raw_y = (uint16_t(aa[1]) << 2) | ((bb[1] >> 5) & 0x02);
    s.raw_z = (uint16_t(aa[2]) << 2) | ((bb[1] >> 6) & 0x02);

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

GuitarHeroState Guitar(const uint8_t *ext, size_t len, bool is_drums) {
    // Guitar Hero (Wii) guitars/drums stream their fret/strum/whammy data
    // packed into the same 6-byte layout as a stock Classic Controller
    // (format 0x01), just with different physical labels on the same
    // button/axis bits - this is how the extension is decoded in practice
    // by the existing open-source drivers (wiiuse's classic_ctrl.c reuses
    // its Classic Controller parser for the guitar for exactly this reason).
    // NOTE: WiiBrew separately states the guitar advertises data-format
    // byte 0x03 (8-bit precision, 8-byte layout) - if your specific guitar
    // reports 8 bytes of extension data instead of 6, this mapping will be
    // wrong and needs to be reworked against a byte capture from real
    // hardware. Treat this decoder as a verified-shape-but-unverified-on-
    // hardware starting point.
    GuitarHeroState s;
    if (len < 6) return s;
    s.connected = true;
    s.is_drums = is_drums;

    const auto cc = Classic(ext, len, /*is_pro=*/false);
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

    // Drum pad velocities are not covered by the Classic-Controller-shaped
    // decode above; GHWT Drums uses extra bytes/mode not modeled here yet.
    return s;
}

BalanceBoardCalibration ParseBalanceBoardCalibration(const uint8_t block32[32]) {
    BalanceBoardCalibration c;
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
    c.valid = true; // CRC32 verification intentionally omitted, see header
    return c;
}

namespace {
// 3-point (0/17/34 kg) piecewise-linear interpolation, per WiiBrew: use the
// two nearest calibration points that bracket the reading, or the top two
// if the reading exceeds the highest calibration point (extrapolate).
float InterpolateWeight(uint16_t raw, uint16_t c0, uint16_t c17, uint16_t c34) {
    // Do the whole computation in the signed/float domain and don't clamp
    // a sub-c0 reading to a hard 0. That clamp used to pin a corner at a
    // flat, unmoving 0.0 kg the instant its raw value dipped even slightly
    // below the stored 0kg calibration point - which happens constantly,
    // both from ordinary sensor drift AND from real physics (the board
    // flexes: when weight shifts toward one corner, the diagonally
    // opposite corner's compression can genuinely drop below its "empty"
    // baseline). WiiBalanceWalker doesn't clamp this either - it just
    // shows the signed raw-vs-baseline delta - which is why its readings
    // stay "alive" on all four corners while ours pinned three of them at
    // exactly 0. Extrapolating below c0 the same way we already
    // extrapolate above c34 fixes that; a final small negative kg reading
    // is expected/harmless (it means "slightly unloaded relative to
    // calibration", not a sensor fault) and callers can clamp for display
    // if they want a strictly-non-negative number.
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
    // Byte layout, cross-checked against two independently-written reference
    // implementations (FreeIMU's Wii Motion Plus support, and the Adafruit/
    // Arduino I2C sample referenced from WiiBrew) since WiiBrew's own bit
    // table for this section is easy to mis-transcribe by hand:
    //   ext[0] = Yaw   low 8 bits      ext[3] bits 7:2 = Yaw   high 6 bits
    //   ext[1] = Roll  low 8 bits      ext[4] bits 7:2 = Roll  high 6 bits
    //   ext[2] = Pitch low 8 bits      ext[5] bits 7:2 = Pitch high 6 bits
    //   ext[3] bit 1 = slow_yaw     (1 = yaw axis in slow/high-precision range)
    //   ext[3] bit 0 = slow_pitch   (1 = pitch axis in slow/high-precision range)
    //   ext[4] bit 1 = slow_roll    (1 = roll axis in slow/high-precision range)
    //   ext[4] bit 0 = extension_connected (per WiiBrew: "usually 1" - a
    //                  passthrough Nunchuk/Classic Controller is present)
    //   ext[5] bit 1 = report-type discriminator: 1 = this IS MotionPlus
    //                  data, 0 = regular extension data snuck through in
    //                  the same byte slot. WiimoteDevice checks this bit
    //                  BEFORE calling this decoder, so it's not re-checked
    //                  here, but note it for anyone reading raw captures.
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

    // Nominal conversion: zero-rate offset ~8192 (14-bit centre - WiiBrew
    // documents the real still-state reading as closer to 0x1F7F/8063, so
    // recalibrate at startup for drift-free readings if precision matters).
    // Scale: ~13.768 counts/deg/s in the "slow"/high-precision range; the
    // "fast" range covers a wider deg/s span (2000 vs 440 deg/s full-scale)
    // over the same 14-bit code space, so its counts/deg/s is smaller by
    // that 2000/440 ratio.
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

} // namespace InputBridge::Wiimote::Decode