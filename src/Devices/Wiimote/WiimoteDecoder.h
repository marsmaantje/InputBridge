// src/Devices/Wiimote/WiimoteDecoder.h
//
// Stateless decode functions from raw report/register bytes to the structs
// in WiimoteState.h. Deliberately free of SDL_hid_* / I/O so they can be
// exercised in tests/ the same way test_haptic_parser.cpp exercises the
// haptic byte parsers - feed known-good byte sequences from WiiBrew and
// assert on the decoded struct.
#pragma once
#include "WiimoteState.h"
#include <cstddef>

namespace InputBridge::Wiimote::Decode {

// First two bytes of every input report except 0x3d.
CoreButtons Buttons(const uint8_t bb[2]);

// 3 accelerometer bytes (XX YY ZZ) + the 2 LSB-bearing button bytes, per
// WiiBrew "Normal Accelerometer Reporting". Pass the same bb[2] used for
// Buttons() above.
AccelState Accel(const uint8_t bb[2], const uint8_t aa[3]);

// Basic-mode IR (10 bytes -> 4 dots, X:10bit/Y:10bit but Y clamped to 0-767
// by the camera). ir[10].
IRState IRBasic(const uint8_t ir[10]);

// Extended-mode IR (12 bytes -> 4 dots, X:10bit/Y:10bit like Basic mode,
// plus a 4-bit dot size per point). Per WiiBrew "IR Camera#Extended Mode":
// 3 bytes per dot - X low 8 bits, Y low 8 bits, then a byte packing X/Y's
// 2 high bits (bits 7-6 / 5-4) with a 4-bit size (bits 3-0). Only
// meaningful while the Wiimote is actually in Extended IR mode (see
// WiimoteDevice::SetIRExtendedMode) - feeding it Basic-mode bytes will
// produce garbage size values, not a decode error, since there's nothing
// in the 12 bytes themselves that identifies which mode produced them.
// ir[12].
IRState IRExtended(const uint8_t ir[12]);

// Extension payload starting at logical extension-register offset 0x08,
// i.e. the EE...EE bytes in reports 0x32/0x34/0x35/0x36/0x37/0x3d. `len`
// must be >= the bytes required for the given decoder or it returns a
// disconnected/zeroed struct.
NunchukState             Nunchuk(const uint8_t *ext, size_t len);
ClassicControllerState    Classic(const uint8_t *ext, size_t len, bool is_pro);
GuitarHeroState            Guitar(const uint8_t *ext, size_t len, bool is_drums);

// Same 6-byte shape as GuitarHeroState's underlying Classic-Controller-
// format decode, factored out so both Guitar() and the MotionPlus
// passthrough path (GuitarFromClassic() below) can build a GuitarHeroState
// from an already-decoded ClassicControllerState instead of duplicating
// the fret/strum/whammy remap.
GuitarHeroState GuitarFromClassic(const ClassicControllerState &cc, bool is_drums);

// Balance Board: 8 weight bytes + temperature + battery, per WiiBrew
// "Wii Balance Board#Data Format". `cal` must be filled in first via
// ParseBalanceBoardCalibration() from the 32-byte block at 0xA40020.
BalanceBoardState BalanceBoard(const uint8_t ext[11], const BalanceBoardCalibration &cal);

// Parses the 32-byte calibration block read from register 0xA40020, and
// verifies it against its trailing CRC32 before trusting it.
//
// Per WiiBrew's "Wii Balance Board#Calibration Data": the checksum is a
// standard CRC32 (reversed polynomial 0xEDB88320, i.e. the same variant as
// zlib/PNG/Ethernet - init 0xFFFFFFFF, final XOR 0xFFFFFFFF), computed over
// 28 bytes in this exact order: the 24 calibration bytes at 0x24-0x3B, then
// the 2 bytes at 0x20-0x21, then the 2 "Reference Temperature" bytes at
// 0x60-0x61 - which live outside this 32-byte block, hence the separate
// `ref_temp2` parameter. The result is stored big-endian in block32's last
// 4 bytes (0x3C-0x3F).
//
// On a CRC mismatch, returns a struct with `valid = false` and all-zero
// calibration arrays rather than the parsed (and possibly corrupted)
// values - a corrupted read must not silently masquerade as good
// calibration data. Callers should keep using the previous known-good
// BalanceBoardCalibration (if any) when this happens rather than
// overwriting it, since the whole point is that a bad read shouldn't
// clobber good data; see WimoteDevice::LoadBalanceBoardCalibration().
BalanceBoardCalibration ParseBalanceBoardCalibration(const uint8_t block32[32],
                                                      const uint8_t ref_temp2[2]);

// Wii Motion Plus, 6-byte extension-format ("DE") passthrough report, per
// WiiBrew "Wiimote/Extension Controllers/Wii Motion Plus#Data Format".
// `ext` is the same extension-offset pointer used by Nunchuk()/Classic()
// above (6 bytes required). This is the MotionPlus's OWN data - when it's
// running in Nunchuk/Classic passthrough mode, the low bits of some
// extension axes are stolen to carry the passthrough device's buttons,
// which WiimoteDevice is responsible for re-merging into the Nunchuk/
// Classic decode separately; this function only concerns itself with the
// gyro rates + connection bits.
MotionPlusState MotionPlus(const uint8_t *ext, size_t len);

// Nunchuk/Classic Controller data as they arrive while a Wii Motion Plus
// is active in that extension's passthrough mode, per WiiBrew's
// "Nunchuck pass-through mode" / "Classic Controller pass-through mode"
// tables (same page as MotionPlus() above). In passthrough, the MotionPlus
// interleaves its own gyro reports with re-encoded extension reports, and
// steals/moves a handful of bits in the extension report to make room for
// 3 bookkeeping bits (an "extension connected" flag and the report-type
// discriminator bit also used by MotionPlus() to tell the two report kinds
// apart). Feeding passthrough-mode bytes to the plain Nunchuk()/Classic()
// decoders instead of these will corrupt an axis LSB or two, and will
// misread the always-zero discriminator/reserved bits in Classic's case as
// permanently-held dpad_left/dpad_up presses - use these whenever
// WimoteDevice has MotionPlus active AND the byte set is extension data
// (i.e. NOT the ee[5] bit 1 == 1 case MotionPlus() itself handles).
// `ext`/`len` are the same extension-offset pointer/length as above.
NunchukState           NunchukViaMotionPlus(const uint8_t *ext, size_t len);
ClassicControllerState ClassicViaMotionPlus(const uint8_t *ext, size_t len, bool is_pro);

} // namespace InputBridge::Wiimote::Decode
