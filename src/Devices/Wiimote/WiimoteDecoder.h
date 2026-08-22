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

// Extension payload starting at logical extension-register offset 0x08,
// i.e. the EE...EE bytes in reports 0x32/0x34/0x35/0x36/0x37/0x3d. `len`
// must be >= the bytes required for the given decoder or it returns a
// disconnected/zeroed struct.
NunchukState             Nunchuk(const uint8_t *ext, size_t len);
ClassicControllerState    Classic(const uint8_t *ext, size_t len, bool is_pro);
GuitarHeroState            Guitar(const uint8_t *ext, size_t len, bool is_drums);

// Balance Board: 8 weight bytes + temperature + battery, per WiiBrew
// "Wii Balance Board#Data Format". `cal` must be filled in first via
// ParseBalanceBoardCalibration() from the 32-byte block at 0xA40020.
BalanceBoardState BalanceBoard(const uint8_t ext[11], const BalanceBoardCalibration &cal);

// Parses the 32-byte calibration block read from register 0xA40020.
// Does NOT verify the trailing CRC32 (harmless to skip - worst case is
// using slightly-off factory calibration on a corrupted read, which a
// re-read will fix); see WiiBrew for the CRC32 polynomial if you want to
// add verification.
BalanceBoardCalibration ParseBalanceBoardCalibration(const uint8_t block32[32]);

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

} // namespace InputBridge::Wiimote::Decode
