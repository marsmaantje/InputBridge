// src/Devices/Wiimote/WiimoteDecoder.cpp
#include "WiimoteDecoder.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace InputBridge::Wiimote::Decode {

namespace {
// Standard CRC32 (reversed polynomial 0xEDB88320 - the zlib/PNG/Ethernet
// variant: init 0xFFFFFFFF, final XOR 0xFFFFFFFF), computed byte-at-a-time
// so this file stays free of any zlib/external dependency. Only used for
// Balance Board calibration verification below; not performance-sensitive
// (28 bytes, once per calibration load), so no lookup table.
uint32_t Crc32(std::span<const std::byte> data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const std::byte byte : data) {
        crc ^= std::to_integer<uint32_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (crc & 1u) != 0u ? 0xFFFFFFFFu : 0u;
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// Raw wire byte -> unsigned integer for shift/mask arithmetic. Everything is
// done on `unsigned` (so no integer-promotion surprises) and narrowed back
// with an explicit Narrow<> at the point a decoded field is stored.
constexpr unsigned U(std::byte b) { return std::to_integer<unsigned>(b); }

template <typename T>
constexpr T Narrow(unsigned value) { return static_cast<T>(value); }

constexpr bool Bit(std::byte b, unsigned mask) { return (U(b) & mask) != 0u; }
} // namespace

CoreButtons Buttons(std::span<const std::byte, 2> bb) {
    CoreButtons s;
    s.left  = Bit(bb[0], 0x01);
    s.right = Bit(bb[0], 0x02);
    s.down  = Bit(bb[0], 0x04);
    s.up    = Bit(bb[0], 0x08);
    s.plus  = Bit(bb[0], 0x10);
    s.two   = Bit(bb[1], 0x01);
    s.one   = Bit(bb[1], 0x02);
    s.b     = Bit(bb[1], 0x04);
    s.a     = Bit(bb[1], 0x08);
    s.minus = Bit(bb[1], 0x10);
    s.home  = Bit(bb[1], 0x80);
    return s;
}

// Bit-extraction for the accelerometer LSBs embedded in the button bytes.
// This is the extraction used consistently across the open-source Wiimote
// driver ecosystem (wiiuse, cwiid, WiimoteLib); WiiBrew's own bit table for
// this section is not fully column-aligned in its wiki markup, so treat this
// as the community-verified reference rather than a literal wiki transcription.
AccelState Accel(std::span<const std::byte, 2> bb, std::span<const std::byte, 3> aa) {
    AccelState s;
    s.raw_x = Narrow<uint16_t>((U(aa[0]) << 2) | ((U(bb[0]) >> 5) & 0x03u));
    s.raw_y = Narrow<uint16_t>((U(aa[1]) << 2) | ((U(bb[1]) >> 4) & 0x02u));
    s.raw_z = Narrow<uint16_t>((U(aa[2]) << 2) | ((U(bb[1]) >> 5) & 0x02u));

    // Nominal (uncalibrated) conversion: 0g ~= 512, 1g ~= 512 + 128 = 640,
    // per WiiBrew's accelerometer overview. For precise work, read the
    // per-device calibration block from EEPROM (WiimoteDevice::ReadAccelCalibration)
    // and use the real 0g/1g offsets instead of these nominal values.
    constexpr float kZeroG = 512.f;
    constexpr float kOneGCounts = 128.f;
    s.g_x = (static_cast<float>(s.raw_x) - kZeroG) / kOneGCounts;
    s.g_y = (static_cast<float>(s.raw_y) - kZeroG) / kOneGCounts;
    s.g_z = (static_cast<float>(s.raw_z) - kZeroG) / kOneGCounts;
    return s;
}

IRState IRBasic(std::span<const std::byte, 10> ir) {
    IRState out{};
    auto decodePair = [](std::span<const std::byte, 5> p, IRDot &d0, IRDot &d1) {
        d0.x = Narrow<uint16_t>(U(p[0]) | ((U(p[2]) & 0x30u) << 4));
        d0.y = Narrow<uint16_t>(U(p[1]) | ((U(p[2]) & 0xC0u) << 2));
        d1.x = Narrow<uint16_t>(U(p[3]) | ((U(p[2]) & 0x03u) << 8));
        d1.y = Narrow<uint16_t>(U(p[4]) | ((U(p[2]) & 0x0Cu) << 6));
        d0.visible = !(p[0] == 0xFF_b && p[1] == 0xFF_b);
        d1.visible = !(p[3] == 0xFF_b && p[4] == 0xFF_b);
    };
    decodePair(ir.subspan<0, 5>(), out[0], out[1]);
    decodePair(ir.subspan<5, 5>(), out[2], out[3]);
    return out;
}

IRState IRExtended(std::span<const std::byte, 12> ir) {
    IRState out{};
    // Per dot: byte0 = X low 8 bits, byte1 = Y low 8 bits, byte2 = Y-high
    // (bits 7:6), X-high (bits 5:4), size (bits 3:0) - same Y-before-X
    // nibble order as Basic Mode's pair-packing byte.
    // An empty slot reads all-1s across all 3 bytes (byte2 == 0xFF in
    // full), so the visibility check must cover the X/Y high-bit nibble
    // too, not just the size nibble - checking only bits 3:0 would
    // misclassify a real dot with size==15 as invisible.
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto p = ir.subspan(i * 3, 3);
        IRDot &d = out[i];
        d.x    = Narrow<uint16_t>(U(p[0]) | ((U(p[2]) & 0x30u) << 4));
        d.y    = Narrow<uint16_t>(U(p[1]) | ((U(p[2]) & 0xC0u) << 2));
        d.size = Narrow<uint8_t>(U(p[2]) & 0x0Fu);
        d.visible = !(p[0] == 0xFF_b && p[1] == 0xFF_b && p[2] == 0xFF_b);
    }
    return out;
}

IRDot IRFullDot(std::span<const std::byte, 9> p) {
    IRDot d{};
    // Per WiiBrew "IR Camera#Full Mode": byte0/1/2 are the same X/Y/size
    // packing as Extended mode. Bytes 3-6 are the bounding box (each only
    // 7 bits wide - bit 7 is always 0, so no masking beyond & 0x7F is
    // needed), byte 7 is unused/reserved, byte 8 is intensity.
    d.x    = Narrow<uint16_t>(U(p[0]) | ((U(p[2]) & 0x30u) << 4));
    d.y    = Narrow<uint16_t>(U(p[1]) | ((U(p[2]) & 0xC0u) << 2));
    d.size = Narrow<uint8_t>(U(p[2]) & 0x0Fu);
    d.visible = !(p[0] == 0xFF_b && p[1] == 0xFF_b && p[2] == 0xFF_b);
    d.bbox_min_x = Narrow<uint8_t>(U(p[3]) & 0x7Fu);
    d.bbox_min_y = Narrow<uint8_t>(U(p[4]) & 0x7Fu);
    d.bbox_max_x = Narrow<uint8_t>(U(p[5]) & 0x7Fu);
    d.bbox_max_y = Narrow<uint8_t>(U(p[6]) & 0x7Fu);
    d.intensity  = Narrow<uint8_t>(U(p[8]));
    return d;
}

NunchukState Nunchuk(std::span<const std::byte> ext) {
    NunchukState s;
    if (ext.size() < 6) return s; // disconnected/insufficient data
    s.connected = true;
    s.stick_x = Narrow<uint8_t>(U(ext[0]));
    s.stick_y = Narrow<uint8_t>(U(ext[1]));
    s.accel_x = Narrow<uint16_t>((U(ext[2]) << 2) | ((U(ext[5]) >> 2) & 0x03u));
    s.accel_y = Narrow<uint16_t>((U(ext[3]) << 2) | ((U(ext[5]) >> 4) & 0x03u));
    s.accel_z = Narrow<uint16_t>((U(ext[4]) << 2) | ((U(ext[5]) >> 6) & 0x03u));
    s.button_c = !Bit(ext[5], 0x02); // 0 = pressed
    s.button_z = !Bit(ext[5], 0x01);
    return s;
}

ClassicControllerState Classic(std::span<const std::byte> ext, bool is_pro) {
    ClassicControllerState s;
    if (ext.size() < 6) return s;
    s.connected = true;
    s.is_pro = is_pro;

    const unsigned b0 = U(ext[0]);
    const unsigned b1 = U(ext[1]);
    const unsigned b2 = U(ext[2]);
    const unsigned b3 = U(ext[3]);

    s.left_x  = Narrow<uint16_t>(b0 & 0x3Fu);
    s.left_y  = Narrow<uint16_t>(b1 & 0x3Fu);
    s.right_x = Narrow<uint8_t>((((b0 >> 6) & 0x03u) << 3) | (((b1 >> 6) & 0x03u) << 1) | ((b2 >> 7) & 0x01u));
    s.right_y = Narrow<uint8_t>(b2 & 0x1Fu);
    s.left_trigger  = Narrow<uint8_t>((((b2 >> 5) & 0x03u) << 3) | ((b3 >> 5) & 0x07u));
    s.right_trigger = Narrow<uint8_t>(b3 & 0x1Fu);

    // All buttons are active-low (0 = pressed) on the wire.
    s.dpad_right = !Bit(ext[4], 0x80);
    s.dpad_down  = !Bit(ext[4], 0x40);
    s.l          = !Bit(ext[4], 0x20);
    s.minus      = !Bit(ext[4], 0x10);
    s.home       = !Bit(ext[4], 0x08);
    s.plus       = !Bit(ext[4], 0x04);
    s.r          = !Bit(ext[4], 0x02);

    s.zl    = !Bit(ext[5], 0x80);
    s.b     = !Bit(ext[5], 0x40);
    s.y     = !Bit(ext[5], 0x20);
    s.a     = !Bit(ext[5], 0x10);
    s.x     = !Bit(ext[5], 0x08);
    s.zr    = !Bit(ext[5], 0x04);
    s.dpad_left = !Bit(ext[5], 0x02);
    s.dpad_up   = !Bit(ext[5], 0x01);

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
    s.stick_x     = Narrow<uint8_t>(cc.left_x * 4u); // rescale 0-63 -> ~0-252
    s.whammy_bar  = Narrow<uint8_t>(cc.right_trigger * 8u); // rescale 0-31 -> ~0-248

    // Drum pad velocities aren't covered by this Classic-shaped decode;
    // GHWT Drums needs extra bytes/mode not modeled here yet.
    return s;
}

GuitarHeroState Guitar(std::span<const std::byte> ext, bool is_drums) {
    // GH guitars/drums stream fret/strum/whammy in the same 6-byte layout
    // as a stock Classic Controller (format 0x01), just with different
    // physical labels - same approach wiiuse's classic_ctrl.c takes.
    // WiiBrew notes the guitar actually advertises format 0x03 (8-bit,
    // 8-byte layout); if real hardware sends 8 bytes this mapping is wrong
    // and needs reworking against a real capture. Unverified on hardware.
    if (ext.size() < 6) return GuitarHeroState{};
    return GuitarFromClassic(Classic(ext, /*is_pro=*/false), is_drums);
}

BalanceBoardCalibration ParseBalanceBoardCalibration(std::span<const std::byte, 32> block32,
                                                      std::span<const std::byte, 2> ref_temp2) {
    BalanceBoardCalibration c;

    // Checksum input, built in the exact (non-contiguous) order WiiBrew
    // documents: 0x24-0x3B (24 bytes), then 0x20-0x21 (2 bytes), then the
    // Reference Temperature bytes at 0x60-0x61 (2 bytes) - 28 bytes total.
    // block32[] is register-relative to 0x20, so 0x24-0x3B is block32[4..27]
    // and 0x20-0x21 is block32[0..1].
    std::array<std::byte, 28> crc_input{};
    std::ranges::copy(block32.subspan<4, 24>(), crc_input.begin());
    crc_input[24] = block32[0];
    crc_input[25] = block32[1];
    crc_input[26] = ref_temp2[0];
    crc_input[27] = ref_temp2[1];

    const uint32_t computed = Crc32(crc_input);
    const uint32_t stored = (std::to_integer<uint32_t>(block32[0x3C - 0x20]) << 24) |
                            (std::to_integer<uint32_t>(block32[0x3D - 0x20]) << 16) |
                            (std::to_integer<uint32_t>(block32[0x3E - 0x20]) << 8) |
                             std::to_integer<uint32_t>(block32[0x3F - 0x20]);
    if (computed != stored) {
        // Corrupted/torn read: don't hand back numbers that look plausible
        // but aren't verified - leave `valid` false (all-zero arrays) so
        // BalanceBoard() skips kg conversion and the caller can decide to
        // keep whatever calibration it already had instead.
        return c;
    }

    auto be16 = [&](std::size_t off) { return ToU16BE(block32[off], block32[off + 1]); };
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
        return 17.f * (static_cast<float>(raw) - static_cast<float>(c0)) / static_cast<float>(c17 - c0);
    }
    if (c34 == c17) return 17.f;
    return 17.f + 17.f * (float(raw) - float(c17)) / float(c34 - c17);
}
} // namespace

BalanceBoardState BalanceBoard(std::span<const std::byte, 11> ext, const BalanceBoardCalibration &cal) {
    BalanceBoardState s;
    s.connected = true;
    s.raw_top_right    = ToU16BE(ext[0], ext[1]);
    s.raw_bottom_right = ToU16BE(ext[2], ext[3]);
    s.raw_top_left     = ToU16BE(ext[4], ext[5]);
    s.raw_bottom_left  = ToU16BE(ext[6], ext[7]);
    s.temperature_raw  = Narrow<uint8_t>(U(ext[8]));
    s.battery_raw      = Narrow<uint8_t>(U(ext[10]));

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

MotionPlusState MotionPlus(std::span<const std::byte> ext) {
    // Byte layout (cross-checked against FreeIMU and Adafruit reference
    // implementations, since WiiBrew's own bit table is easy to mistranscribe):
    //   ext[0/1/2] = Yaw/Roll/Pitch low 8 bits
    //   ext[3/4/5] bits 7:2 = Yaw/Roll/Pitch high 6 bits
    //   ext[3] bit 1/0 = slow_yaw/slow_pitch; ext[4] bit 1 = slow_roll
    //   ext[4] bit 0 = extension_connected (passthrough device present)
    //   ext[5] bit 1 = report-type discriminator (1 = MotionPlus data) -
    //     WiimoteDevice checks this before calling here, not re-checked.
    MotionPlusState s;
    if (ext.size() < 6) return s; // disconnected/insufficient data
    s.connected = true;

    s.raw_yaw   = Narrow<uint16_t>(U(ext[0]) | ((U(ext[3]) & 0xFCu) << 6));
    s.raw_roll  = Narrow<uint16_t>(U(ext[1]) | ((U(ext[4]) & 0xFCu) << 6));
    s.raw_pitch = Narrow<uint16_t>(U(ext[2]) | ((U(ext[5]) & 0xFCu) << 6));

    s.slow_yaw   = Bit(ext[3], 0x02);
    s.slow_pitch = Bit(ext[3], 0x01);
    s.slow_roll  = Bit(ext[4], 0x02);

    s.extension_connected = Bit(ext[4], 0x01);

    // Nominal conversion: zero-rate offset 8192 (14-bit centre; real
    // hardware idles closer to ~8063, so recalibrate at startup for
    // precision). Scale: ~13.768 counts/deg/s in "slow" range; "fast"
    // range covers 2000 vs 440 deg/s full-scale over the same code space.
    constexpr float kZero = 8192.f;
    constexpr float kSlowCountsPerDegS = 8192.f / 595.f; // ~13.768
    constexpr float kFastCountsPerDegS = kSlowCountsPerDegS * 440.f / 2000.f;
    auto toDegS = [&](uint16_t raw, bool slow) {
        const float countsPerDegS = slow ? kSlowCountsPerDegS : kFastCountsPerDegS;
        return (static_cast<float>(raw) - kZero) / countsPerDegS;
    };
    s.deg_s_yaw   = toDegS(s.raw_yaw,   s.slow_yaw);
    s.deg_s_pitch = toDegS(s.raw_pitch, s.slow_pitch);
    s.deg_s_roll  = toDegS(s.raw_roll,  s.slow_roll);

    return s;
}

NunchukState NunchukViaMotionPlus(std::span<const std::byte> ext) {
    // Per WiiBrew "Nunchuck pass-through mode": SX/SY untouched; each accel
    // axis loses its LSB (always 0 here) to make room for bookkeeping bits
    // relocated into ext[5]:
    //   bit 5/4 = AY/AX bit 1     bits 7:6 = AZ bits 2:1
    //   bit 3/2 = Button C/Z (active-low)   bits 1:0 = discriminator/reserved
    // AZ's top 7 bits stay in ext[4] bits 7:1; ext[4] bit 0 becomes
    // "extension connected".
    NunchukState s;
    if (ext.size() < 6) return s; // disconnected/insufficient data
    s.connected = true;
    s.stick_x = Narrow<uint8_t>(U(ext[0]));
    s.stick_y = Narrow<uint8_t>(U(ext[1]));
    s.accel_x = Narrow<uint16_t>((U(ext[2]) << 2) | (((U(ext[5]) >> 4) & 0x01u) << 1));
    s.accel_y = Narrow<uint16_t>((U(ext[3]) << 2) | (((U(ext[5]) >> 5) & 0x01u) << 1));
    s.accel_z = Narrow<uint16_t>(((U(ext[4]) >> 1) << 3) | (((U(ext[5]) >> 6) & 0x03u) << 1));
    s.button_c = !Bit(ext[5], 0x08);
    s.button_z = !Bit(ext[5], 0x04);
    return s;
}

ClassicControllerState ClassicViaMotionPlus(std::span<const std::byte> ext, bool is_pro) {
    // Per WiiBrew "Classic Controller pass-through mode": RX/RY/LT/RT and
    // all buttons except the D-pad sit at the same bits as Classic() above.
    // What differs: left stick X/Y each lose their LSB to make room for
    // BDU (moved to ext[0] bit 0) and BDL (moved to ext[1] bit 0); ext[4]
    // bit 0 becomes "extension connected"; ext[5] bits 1:0 become the
    // discriminator/reserved bits instead of dpad_left/up (reading them as
    // buttons, as plain Classic() would, misreports both as held).
    ClassicControllerState s;
    if (ext.size() < 6) return s;
    s.connected = true;
    s.is_pro = is_pro;

    const unsigned b0 = U(ext[0]);
    const unsigned b1 = U(ext[1]);
    const unsigned b2 = U(ext[2]);
    const unsigned b3 = U(ext[3]);

    s.left_x  = Narrow<uint16_t>(b0 & 0x3Eu); // LX<5:1>, bit 0 forced to 0 (stolen for BDU)
    s.left_y  = Narrow<uint16_t>(b1 & 0x3Eu); // LY<5:1>, bit 0 forced to 0 (stolen for BDL)
    s.right_x = Narrow<uint8_t>((((b0 >> 6) & 0x03u) << 3) | (((b1 >> 6) & 0x03u) << 1) | ((b2 >> 7) & 0x01u));
    s.right_y = Narrow<uint8_t>(b2 & 0x1Fu);
    s.left_trigger  = Narrow<uint8_t>((((b2 >> 5) & 0x03u) << 3) | ((b3 >> 5) & 0x07u));
    s.right_trigger = Narrow<uint8_t>(b3 & 0x1Fu);

    s.dpad_right = !Bit(ext[4], 0x80);
    s.dpad_down  = !Bit(ext[4], 0x40);
    s.l          = !Bit(ext[4], 0x20);
    s.minus      = !Bit(ext[4], 0x10);
    s.home       = !Bit(ext[4], 0x08);
    s.plus       = !Bit(ext[4], 0x04);
    s.r          = !Bit(ext[4], 0x02);
    // ext[4] bit 0 here is "extension connected", not a button.

    s.zl    = !Bit(ext[5], 0x80);
    s.b     = !Bit(ext[5], 0x40);
    s.y     = !Bit(ext[5], 0x20);
    s.a     = !Bit(ext[5], 0x10);
    s.x     = !Bit(ext[5], 0x08);
    s.zr    = !Bit(ext[5], 0x04);
    // ext[5] bits 1:0 are the discriminator/reserved bits, not dpad_left/up.

    s.dpad_up   = !Bit(ext[0], 0x01);
    s.dpad_left = !Bit(ext[1], 0x01);

    return s;
}

} // namespace InputBridge::Wiimote::Decode
