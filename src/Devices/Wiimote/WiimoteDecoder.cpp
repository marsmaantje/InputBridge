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
    if (raw < c0) return 0.f;
    if (raw <= c17) {
        if (c17 == c0) return 0.f;
        return 17.f * float(raw - c0) / float(c17 - c0);
    }
    if (c34 == c17) return 17.f;
    return 17.f + 17.f * float(raw - c17) / float(c34 - c17);
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

} // namespace InputBridge::Wiimote::Decode
