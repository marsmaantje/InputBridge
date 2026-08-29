// src/Devices/Wiimote/WiimoteState.h
//
// Plain decoded-value structs, analogous to Devices/SensorState.h. These are
// what the rest of InputBridge (visualizer, protocol field mapper) actually
// reads - never the raw report bytes.
#pragma once
#include "WiimoteProtocol.h"
#include <cstdint>
#include <array>

namespace InputBridge::Wiimote {

// SDL_GetTicks()-scale timestamp of the last input report received. Used by
// DeviceManager to prune Wiimotes that went silent (unplugged / out of BT
// range) - SDL_hid has no disconnect callback, so staleness is the only
// signal available without deeper platform-specific HID hotplug code.
using TimestampMs = uint64_t;

struct CoreButtons {
    bool left = false, right = false, down = false, up = false;
    bool plus = false, minus = false, home = false;
    bool one = false, two = false, a = false, b = false;
};

// Raw accel bytes are 0-1023 (10-bit), ~0x200 (512) at 0g, scale is device
// specific (calibration in EEPROM 0x16/0x20); we expose both raw and a
// best-effort "g" value using the *uncalibrated* nominal 0g=512, 1g=~640
// mid-point documented on WiiBrew as a reasonable default. For precise
// physics work, read the EEPROM calibration block (0x0000-0x0020) instead -
// hook is provided in WiimoteDevice::ReadAccelCalibration().
struct AccelState {
    uint16_t raw_x = 512, raw_y = 512, raw_z = 512; // 10-bit
    float g_x = 0.f, g_y = 0.f, g_z = 1.f;
};

// One tracked IR point. Basic mode gives 10-bit X (0-1023) / Y (0-767);
// an empty slot is reported as fully off (visible=false). Extended mode
// (see WiimoteDevice::SetIRExtendedMode) additionally gives a 4-bit dot
// size (0-15, larger = bigger/brighter IR blob as seen by the camera) but
// truncates X/Y to 8 bits internally before re-expanding them, and drops
// all extension (Nunchuk/Classic/Guitar) data for as long as it's active -
// see the comment on SetIRExtendedMode() for why. `size` stays 0 (its
// at-rest value) whenever extended mode isn't active.
struct IRDot {
    bool visible = false;
    uint16_t x = 0, y = 0;
    uint8_t size = 0; // 0-15, extended mode only
};
using IRState = std::array<IRDot, 4>;

struct NunchukState {
    bool connected = false;
    uint8_t stick_x = 128, stick_y = 128;   // ~35-228 / ~27-220 range, 128 center
    uint16_t accel_x = 512, accel_y = 512, accel_z = 512; // 10-bit, same scale as Wiimote accel
    bool button_c = false, button_z = false;
};

struct ClassicControllerState {
    bool connected = false;
    bool is_pro = false; // Classic Controller Pro (digital-only triggers)
    uint16_t left_x = 32, left_y = 32;   // 0-63 (format 0x01)
    uint8_t  right_x = 16, right_y = 16; // 0-31 (format 0x01)
    uint8_t  left_trigger = 0, right_trigger = 0; // 0-31 analog, or digital 0/31 on Pro
    bool dpad_up = false, dpad_down = false, dpad_left = false, dpad_right = false;
    bool a = false, b = false, x = false, y = false;
    bool l = false, r = false;        // digital click of triggers
    bool zl = false, zr = false;
    bool plus = false, minus = false, home = false;
};

// Guitar Hero (Wii) Guitar / Drums - reported in Classic-Controller-format
// 0x03 (8-bit precision), per WiiBrew. Fret/strum/whammy mapping below
// follows the widely-documented community mapping (WiimoteLib, wiiuse,
// GH3/GHWT PC drivers); verify against wiibrew.org/wiki/Wiimote/Extension_Controllers/Guitar_Hero_(Wii)_Guitars
// before shipping if exact fidelity matters for your use case.
struct GuitarHeroState {
    bool connected = false;
    bool is_drums = false;
    bool fret_green = false, fret_red = false, fret_yellow = false;
    bool fret_blue = false, fret_orange = false;
    bool strum_up = false, strum_down = false;
    bool plus = false, minus = false;
    uint8_t whammy_bar = 0;  // 0 (released) - ~255 (fully depressed), raw analog
    uint8_t stick_x = 128;   // analog "joystick" nub on the guitar neck

    // Drums only: velocity-sensitive pads, cymbal, kick pedal (needs
    // extension data format verification for GHWT Drums specifically).
    bool drum_kick = false;
    std::array<uint8_t, 6> drum_velocity{}; // red, yellow, blue, green, orange, hi-hat
};

// Wii Balance Board. Weight units are kilograms, computed via 3-point
// (0/17/34 kg) linear interpolation against the board's own calibration
// data, per wiibrew.org/wiki/Wii_Balance_Board#Calibration_Data.
struct BalanceBoardState {
    bool connected = false; // always true for a device identified as a Balance Board
    bool button_a = false;  // the board's single physical button
    uint16_t raw_top_right = 0, raw_bottom_right = 0, raw_top_left = 0, raw_bottom_left = 0;
    float kg_top_right = 0.f, kg_bottom_right = 0.f, kg_top_left = 0.f, kg_bottom_left = 0.f;
    float kg_total = 0.f;
    // Center of gravity in board-relative coordinates, [-1, 1] on each axis
    // (0,0 = center). Derived the same way WiimoteLib/Wii Fit compute it:
    // weighted average of the four corner readings.
    float cog_x = 0.f, cog_y = 0.f;
    uint8_t battery_raw = 0; // see WiiBrew battery-level thresholds
    uint8_t temperature_raw = 0;
};

// Per-sensor 3-point calibration used for the interpolation above.
struct BalanceBoardCalibration {
    // True only once a calibration block has been parsed AND its trailing
    // CRC32 has been checked against the board's own checksum (see
    // ParseBalanceBoardCalibration() in WiimoteDecoder.h/.cpp). A block that
    // fails the CRC leaves this false rather than handing back possibly-
    // corrupted numbers - BalanceBoard() below zeroes the kg fields whenever
    // `valid` is false.
    bool valid = false;
    uint16_t kg0[4]  = {0, 0, 0, 0}; // order: TR, BR, TL, BL
    uint16_t kg17[4] = {0, 0, 0, 0};
    uint16_t kg34[4] = {0, 0, 0, 0};
};

// Wii Motion Plus, read via its 6-byte extension-format passthrough report
// (the "DE" data format, WiiBrew "Wiimote/Extension Controllers/Wii Motion
// Plus"). Yaw/pitch/roll are angular *rates* (deg/s), not absolute
// orientation - integrate over time yourself if you need an angle.
// `slow_*` flags mark which axes were captured at the "slow"/high-precision
// gyroscope range for that sample (WiiBrew: ~440 deg/s full-scale vs. the
// "fast" range's ~2000 deg/s, both over the same 14-bit code space), needed
// to pick the right zero-offset/scale when converting.
struct MotionPlusState {
    bool connected = false;
    bool is_nunchuk_passthrough = false;  // MotionPlus + Nunchuk daisy-chained
    bool is_classic_passthrough = false;  // MotionPlus + Classic Controller daisy-chained
    bool extension_connected = false;     // bit reported by MotionPlus itself (byte 4 bit 0)

    uint16_t raw_yaw = 8192, raw_pitch = 8192, raw_roll = 8192; // 14-bit, ~8192 = 0 deg/s
    bool slow_yaw = false, slow_pitch = false, slow_roll = false;

    // Best-effort deg/s conversion using WiiBrew's documented nominal
    // zero-offset (8192, though real hardware idles closer to ~8063 - worth
    // a runtime calibration pass) and its documented voltage-reference-
    // derived scale (~13.768 counts/deg/s in the slow/high-precision range,
    // scaled by 2000/440 for the fast range). Per-device zero calibration
    // lives in the MotionPlus's own calibration registers (0xA60020) if
    // precise drift-free readings are needed later.
    float deg_s_yaw = 0.f, deg_s_pitch = 0.f, deg_s_roll = 0.f;
};

enum class BatteryBars { Empty, One, Two, Three, Four };
inline BatteryBars ClassifyWiimoteBattery(uint8_t raw) {
    // Thresholds from WiiBrew's Balance Board page; same scale is used by
    // the Wiimote's own 0x20 status report battery byte.
    if (raw >= 0x82) return BatteryBars::Four;
    if (raw >= 0x7D) return BatteryBars::Three;
    if (raw >= 0x78) return BatteryBars::Two;
    if (raw >= 0x6A) return BatteryBars::One;
    return BatteryBars::Empty;
}

} // namespace InputBridge::Wiimote
