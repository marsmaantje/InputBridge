// src/Devices/Wiimote/WiimoteDecoder.h
//
// Stateless decode functions from raw report/register bytes to the structs
// in WiimoteState.h. No SDL_hid_*/I/O, so they're unit-testable with known
// byte sequences from WiiBrew (tests/test_wiimote_decoder.cpp).
#pragma once
#include "WiimoteState.h"
#include <cstddef>

namespace InputBridge::Wiimote::Decode {

// First two bytes of every input report except 0x3d.
CoreButtons Buttons(const uint8_t bb[2]);

// 3 accel bytes (XX YY ZZ) plus the LSBs packed into bb[2], per WiiBrew
// "Normal Accelerometer Reporting". Pass the same bb[2] used for Buttons().
AccelState Accel(const uint8_t bb[2], const uint8_t aa[3]);

// Basic-mode IR: 10 bytes -> 4 dots, 10-bit X, 10-bit Y (camera clamps Y to
// 0-767).
IRState IRBasic(const uint8_t ir[10]);

// Extended-mode IR: 12 bytes -> 4 dots, same X/Y as Basic plus a 4-bit dot
// size. Only valid while the Wiimote is actually in Extended IR mode
// (WiimoteDevice::SetIRMode(IRCameraMode::Extended)) - the bytes don't
// self-identify their mode, so feeding Basic-mode bytes here silently
// produces garbage sizes.
IRState IRExtended(const uint8_t ir[12]);

// Full-mode IR: one dot's worth of the interleaved 0x3e/0x3f report pair (9
// bytes each, per WiiBrew "Wiimote#0x3e/0x3f: Interleaved Core Buttons and
// Accelerometer with IR" - two dots per report, four dots total across the
// pair). Byte layout per dot, WiiBrew "IR Camera#Full Mode":
//   byte0    = X low 8 bits
//   byte1    = Y low 8 bits
//   byte2    = Y-high(7:6) X-high(5:4) size(3:0)  - same nibble order as
//              Extended mode's packing byte
//   byte3    = bounding box min X
//   byte4    = bounding box min Y
//   byte5    = bounding box max X
//   byte6    = bounding box max Y
//   byte7    = unused/reserved
//   byte8    = intensity
// An empty slot reads all-1s across byte0/byte1/byte2, matching Extended
// mode's sentinel. Only valid while the Wiimote is actually in Full IR mode
// (WiimoteDevice::SetIRMode(IRCameraMode::Full)) - like IRExtended(), these
// bytes don't self-identify their mode.
//
// Each 0x3e/0x3f report carries two of these 9-byte blocks (two objects);
// callers decode both halves and merge across the interleaved report pair
// into a single 4-dot IRState. See WiimoteDevice::DecodeInterleavedIR()
// for how the two reports combine.
IRDot IRFullDot(const uint8_t nine_bytes[9]);

// Extension payload starting at extension-register offset 0x08 (the EE...EE
// bytes in reports 0x32/0x34/0x35/0x36/0x37/0x3d). `len` must cover the
// bytes the given decoder needs, else it returns a disconnected/zeroed
// struct.
NunchukState             Nunchuk(const uint8_t *ext, size_t len);
ClassicControllerState    Classic(const uint8_t *ext, size_t len, bool is_pro);
GuitarHeroState            Guitar(const uint8_t *ext, size_t len, bool is_drums);

// Builds a GuitarHeroState from an already-decoded ClassicControllerState -
// shared by Guitar() and the MotionPlus passthrough path below so the
// fret/strum/whammy remap isn't duplicated.
GuitarHeroState GuitarFromClassic(const ClassicControllerState &cc, bool is_drums);

// Balance Board: 8 weight bytes + temperature + battery (WiiBrew "Wii
// Balance Board#Data Format"). `cal` must come from
// ParseBalanceBoardCalibration() first.
BalanceBoardState BalanceBoard(const uint8_t ext[11], const BalanceBoardCalibration &cal);

// Parses the 32-byte calibration block at register 0xA40020 and verifies it
// against its trailing CRC32 before trusting it.
//
// Checksum is standard CRC32 (reversed poly 0xEDB88320, zlib/PNG/Ethernet
// variant) over 28 bytes in this order: 0x24-0x3B (24 bytes), then
// 0x20-0x21, then the Reference Temperature bytes at 0x60-0x61 (outside
// this block, hence the separate `ref_temp2` param). Stored big-endian in
// block32's last 4 bytes.
//
// On mismatch, returns `valid = false` with zeroed arrays rather than
// possibly-corrupted values - callers should keep any previous known-good
// calibration instead of overwriting it (see
// WiimoteDevice::LoadBalanceBoardCalibration()).
BalanceBoardCalibration ParseBalanceBoardCalibration(const uint8_t block32[32],
                                                      const uint8_t ref_temp2[2]);

// Wii Motion Plus's own 6-byte "DE" data (WiiBrew's Wii Motion Plus page).
// `ext` is the same extension-offset pointer as Nunchuk()/Classic() (6
// bytes). Covers gyro rates + connection bits only - re-merging a
// passthrough device's buttons is WiimoteDevice's job, via the *ViaMotionPlus
// decoders below.
MotionPlusState MotionPlus(const uint8_t *ext, size_t len);

// Nunchuk/Classic Controller data as it arrives while a Motion Plus is
// active in that extension's passthrough mode (WiiBrew's "Nunchuck/Classic
// Controller pass-through mode" tables). Passthrough steals/shifts a few
// bits to make room for bookkeeping, so feeding this data to the plain
// Nunchuk()/Classic() decoders instead corrupts an axis LSB or two and can
// misread reserved bits as stuck dpad presses. Use these whenever MotionPlus
// is active and the byte set is extension data (not the ee[5] bit-1 gyro
// case MotionPlus() handles). Same `ext`/`len` shape as above.
NunchukState           NunchukViaMotionPlus(const uint8_t *ext, size_t len);
ClassicControllerState ClassicViaMotionPlus(const uint8_t *ext, size_t len, bool is_pro);

} // namespace InputBridge::Wiimote::Decode
