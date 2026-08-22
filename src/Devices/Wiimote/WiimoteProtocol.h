// src/Devices/Wiimote/WiimoteProtocol.h
//
// Wire-protocol constants for the Wii Remote / Wii Remote Plus / Wii Balance
// Board, taken from https://wiibrew.org/wiki/Wiimote and
// https://wiibrew.org/wiki/Wiimote/Extension_Controllers and
// https://wiibrew.org/wiki/Wii_Balance_Board (retrieved 2026-08-15).
//
// This header intentionally contains ONLY numeric constants + tiny structs -
// no I/O - so it can be unit tested without a real device or SDL_hid handle.
#pragma once
#include <cstdint>
#include <array>

namespace InputBridge::Wiimote {

// -- USB VID/PID (also used over the Bluetooth HID transport) --------------
constexpr uint16_t kVendorNintendo        = 0x057e;
constexpr uint16_t kProductWiimote        = 0x0306; // RVL-CNT-01
constexpr uint16_t kProductWiimotePlus    = 0x0330; // RVL-CNT-01-TR (incl. Balance Board)

// -- Output report IDs (host -> Wiimote) ------------------------------------
namespace OutReport {
    constexpr uint8_t Rumble          = 0x10;
    constexpr uint8_t LEDs            = 0x11;
    constexpr uint8_t DataReportMode  = 0x12;
    constexpr uint8_t IRCameraEnable1 = 0x13;
    constexpr uint8_t SpeakerEnable   = 0x14;
    constexpr uint8_t StatusRequest   = 0x15;
    constexpr uint8_t WriteMemory     = 0x16;
    constexpr uint8_t ReadMemory      = 0x17;
    constexpr uint8_t SpeakerData     = 0x18;
    constexpr uint8_t SpeakerMute     = 0x19;
    constexpr uint8_t IRCameraEnable2 = 0x1a;
}

// -- Input report IDs (Wiimote -> host) --------------------------------------
namespace InReport {
    constexpr uint8_t Status          = 0x20;
    constexpr uint8_t ReadMemoryData  = 0x21;
    constexpr uint8_t Acknowledge     = 0x22;

    constexpr uint8_t Core              = 0x30; // buttons only
    constexpr uint8_t CoreAccel         = 0x31; // buttons + accel
    constexpr uint8_t CoreExt8          = 0x32; // buttons + 8 ext bytes (Balance Board default)
    constexpr uint8_t CoreAccelIR12     = 0x33; // buttons + accel + 12 IR bytes (basic/ext IR)
    constexpr uint8_t CoreExt19         = 0x34; // buttons + 19 ext bytes (Balance Board + battery)
    constexpr uint8_t CoreAccelExt16    = 0x35; // buttons + accel + 16 ext bytes
    constexpr uint8_t CoreIR10Ext9      = 0x36; // buttons + 10 IR + 9 ext
    constexpr uint8_t CoreAccelIR10Ext6 = 0x37; // buttons + accel + 10 IR (basic mode) + 6 ext  <- primary mode we use
    constexpr uint8_t Ext21             = 0x3d; // 21 ext bytes only
    constexpr uint8_t InterleavedA      = 0x3e; // interleaved accel+IR (full mode), half 1
    constexpr uint8_t InterleavedB      = 0x3f; // interleaved accel+IR (full mode), half 2
}

// -- Memory / register address space ----------------------------------------
// Bit 2 (0x04) of the flags byte in ReadMemory/WriteMemory selects Control
// Registers instead of EEPROM. Must always be set for anything below.
constexpr uint8_t kRegisterFlag = 0x04;

namespace Registers {
    constexpr uint32_t SpeakerBase       = 0xA20000; // - 0xA20009
    constexpr uint32_t ExtensionBase     = 0xA40000; // - 0xA400FF
    constexpr uint32_t ExtensionInitNew1 = 0xA400F0; // write 0x55 (new-style unencrypted init, step 1)
    constexpr uint32_t ExtensionInitNew2 = 0xA400FB; // write 0x00 (new-style unencrypted init, step 2)
    constexpr uint32_t ExtensionCalib    = 0xA40020; // Balance Board calibration block start
    constexpr uint32_t ExtensionData     = 0xA40000; // live data, 11 bytes for Balance Board / 6-8 for others
    constexpr uint32_t ExtensionId       = 0xA400FA; // 6-byte extension ID (Wiimote) / 0xA400FE 2-byte (Balance Board)
    constexpr uint32_t ExtensionIdShort  = 0xA400FE; // 2-byte short form, also the "data format" byte pair
    constexpr uint32_t MotionPlusBase    = 0xA60000; // - 0xA600FF
    constexpr uint32_t MotionPlusId      = 0xA600FA; // 6-byte ID, same shape as ExtensionId
    constexpr uint32_t MotionPlusInit    = 0xA600F0; // write 0x55 (activate, "standalone" mode)
    constexpr uint32_t MotionPlusInitNunchukPass  = 0xA600FE; // write 0x05 (activate w/ Nunchuk passthrough)
    constexpr uint32_t MotionPlusInitClassicPass  = 0xA600FE; // write 0x07 (activate w/ Classic passthrough)
    constexpr uint32_t IRCameraBase      = 0xB00000; // - 0xB00033
    constexpr uint32_t IRSensitivity1    = 0xB00000; // 9-byte block
    constexpr uint32_t IRSensitivity2    = 0xB0001A; // 2-byte block
    constexpr uint32_t IRMode            = 0xB00033; // 1 byte
    constexpr uint32_t IRModeToggle      = 0xB00030; // write 0x08 (or 0x01 per "Wii" sequence variant)
}

// -- Extension identification ------------------------------------------------
// Read 6 bytes from Registers::ExtensionId after the "new way" init
// (write 0x55 -> 0xA400F0, then 0x00 -> 0xA400FB). These bytes come back
// UNENCRYPTED with that init method, so no decrypt step is required.
enum class ExtensionType {
    None,
    Nunchuk,
    ClassicController,
    ClassicControllerPro,
    BalanceBoard,
    GuitarHeroGuitar,
    GuitarHeroDrums,
    MotionPlus,
    Unknown,
};

// 6-byte extension IDs as returned unencrypted, format: XX XX A4 20 ZZ ZZ
struct ExtensionId6 { std::array<uint8_t, 6> bytes; };

inline ExtensionType ClassifyExtension(const ExtensionId6 &id) {
    const auto &b = id.bytes;
    // Guard: must look like a real ID (bytes[2..3] == A4 20 or A6 20),
    // otherwise treat as none/unknown - avoids misclassifying a disconnected
    // slot (all 0xFF) or a mid-handshake read. Regular extensions (Nunchuk,
    // Classic Controller, Balance Board, ...) live at 0xA4xxxx and report
    // A4 20 here; a Motion Plus, however, is read from its own register
    // base at 0xA6xxxx and reports A6 20 in this same position (see
    // WiiBrew "Wii Motion Plus#Identifying" - this is not a typo/alias of
    // A4, the hardware genuinely answers with A6 here).
    if ((b[2] != 0xA4 && b[2] != 0xA6) || b[3] != 0x20) return ExtensionType::Unknown;

    const uint16_t sub = (uint16_t(b[0]) << 8) | b[1]; // XXXX
    const uint16_t typ = (uint16_t(b[4]) << 8) | b[5]; // ZZZZ

    if (typ == 0x0000) return ExtensionType::Nunchuk;
    if (typ == 0x0101) return (sub == 0x0100) ? ExtensionType::ClassicControllerPro
                                               : ExtensionType::ClassicController;
    if (typ == 0x0402) return ExtensionType::BalanceBoard;
    if (typ == 0x0103) return (sub == 0x0100) ? ExtensionType::GuitarHeroDrums
                                               : ExtensionType::GuitarHeroGuitar;
    if (typ == 0x0005 || typ == 0x0405 || typ == 0x0505 || typ == 0x0705)
        return ExtensionType::MotionPlus;

    return ExtensionType::Unknown;
}

// Which passthrough mode a detected MotionPlus ID indicates, per WiiBrew's
// "Wii Motion Plus#Identifying" table. Only meaningful when
// ClassifyExtension() above returned ExtensionType::MotionPlus.
enum class MotionPlusPassthrough { None, Nunchuk, Classic, Unknown };

inline MotionPlusPassthrough ClassifyMotionPlusPassthrough(const ExtensionId6 &id) {
    const auto &b = id.bytes;
    const uint16_t typ = (uint16_t(b[4]) << 8) | b[5];
    switch (typ) {
        case 0x0005: return MotionPlusPassthrough::None;
        case 0x0405: return MotionPlusPassthrough::Nunchuk;
        case 0x0505: return MotionPlusPassthrough::Classic;
        default:     return MotionPlusPassthrough::Unknown;
    }
}

// -- IR camera sensitivity blocks (from WiiBrew "Sensitivity Settings") -----
// Block1 is 9 bytes -> Registers::IRSensitivity1, Block2 is 2 bytes ->
// Registers::IRSensitivity2. "Wii level 3" is what the console itself
// defaults to and is a safe general-purpose choice.
struct IRSensitivity {
    std::array<uint8_t, 9> block1;
    std::array<uint8_t, 2> block2;
};

inline constexpr IRSensitivity kIRSensitivityWiiLevel3 = {
    {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64},
    {0x63, 0x03},
};

// IR data format mode numbers (written to Registers::IRMode)
namespace IRMode {
    constexpr uint8_t Basic    = 1; // 10 bytes, 4 dots, X/Y only - fits report 0x37/0x36
    constexpr uint8_t Extended = 3; // 12 bytes, 4 dots, X/Y + size  - fits report 0x33
    constexpr uint8_t Full     = 5; // 36 bytes, 4 dots, X/Y+size+bbox+intensity - needs 0x3e/0x3f
}

} // namespace InputBridge::Wiimote