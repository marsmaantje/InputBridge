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
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace InputBridge::Wiimote {

// -- Byte helpers --------------------------------------------------------
// Raw wire data (report IDs, register payloads, extension bytes) is held as
// std::byte throughout this module; plain integers are only used for decoded
// numeric values. `0x16_b` spells a std::byte literal without a static_cast
// at every use site; consteval, so an out-of-range value (e.g. 0x1FF_b) is a
// compile error rather than a silent truncation.
consteval std::byte operator""_b(unsigned long long value) {
    if (value > 0xFFull) throw "std::byte literal out of range (> 0xFF)";
    return static_cast<std::byte>(value);
}

// Low 8 bits of `value` as a byte (explicit, intentional truncation).
constexpr std::byte LowByte(uint32_t value) {
    return static_cast<std::byte>(value & 0xFFu);
}

// Big-endian pair of bytes -> uint16_t (hi byte first, as the Wiimote sends
// multi-byte fields).
constexpr uint16_t ToU16BE(std::byte hi, std::byte lo) {
    return static_cast<uint16_t>((std::to_integer<unsigned>(hi) << 8) | std::to_integer<unsigned>(lo));
}

// -- USB VID/PID (also used over the Bluetooth HID transport) --------------
constexpr uint16_t kVendorNintendo        = 0x057e;
constexpr uint16_t kProductWiimote        = 0x0306; // RVL-CNT-01
constexpr uint16_t kProductWiimotePlus    = 0x0330; // RVL-CNT-01-TR (incl. Balance Board)

// -- Output report IDs (host -> Wiimote) ------------------------------------
namespace OutReport {
    constexpr std::byte Rumble          = 0x10_b;
    constexpr std::byte LEDs            = 0x11_b;
    constexpr std::byte DataReportMode  = 0x12_b;
    constexpr std::byte IRCameraEnable1 = 0x13_b;
    constexpr std::byte SpeakerEnable   = 0x14_b;
    constexpr std::byte StatusRequest   = 0x15_b;
    constexpr std::byte WriteMemory     = 0x16_b;
    constexpr std::byte ReadMemory      = 0x17_b;
    constexpr std::byte SpeakerData     = 0x18_b;
    constexpr std::byte SpeakerMute     = 0x19_b;
    constexpr std::byte IRCameraEnable2 = 0x1a_b;
}

// -- Input report IDs (Wiimote -> host) --------------------------------------
namespace InReport {
    constexpr std::byte Status          = 0x20_b;
    constexpr std::byte ReadMemoryData  = 0x21_b;
    constexpr std::byte Acknowledge     = 0x22_b;

    constexpr std::byte Core              = 0x30_b; // buttons only
    constexpr std::byte CoreAccel         = 0x31_b; // buttons + accel
    constexpr std::byte CoreExt8          = 0x32_b; // buttons + 8 ext bytes (Balance Board default)
    constexpr std::byte CoreAccelIR12     = 0x33_b; // buttons + accel + 12 IR bytes (basic/ext IR)
    constexpr std::byte CoreExt19         = 0x34_b; // buttons + 19 ext bytes (Balance Board + battery)
    constexpr std::byte CoreAccelExt16    = 0x35_b; // buttons + accel + 16 ext bytes
    constexpr std::byte CoreIR10Ext9      = 0x36_b; // buttons + 10 IR + 9 ext
    constexpr std::byte CoreAccelIR10Ext6 = 0x37_b; // buttons + accel + 10 IR (basic mode) + 6 ext  <- primary mode we use
    constexpr std::byte Ext21             = 0x3d_b; // 21 ext bytes only
    constexpr std::byte InterleavedA      = 0x3e_b; // interleaved accel+IR (full mode), half 1
    constexpr std::byte InterleavedB      = 0x3f_b; // interleaved accel+IR (full mode), half 2
}

// -- Memory / register address space ----------------------------------------
// Bit 2 (0x04) of the flags byte in ReadMemory/WriteMemory selects Control
// Registers instead of EEPROM. Must always be set for anything below.
constexpr std::byte kRegisterFlag = 0x04_b;

namespace Registers {
    constexpr uint32_t SpeakerBase       = 0xA20000; // - 0xA20009
    constexpr uint32_t ExtensionBase     = 0xA40000; // - 0xA400FF
    constexpr uint32_t ExtensionInitNew1 = 0xA400F0; // write 0x55 (new-style unencrypted init, step 1)
    constexpr uint32_t ExtensionInitNew2 = 0xA400FB; // write 0x00 (new-style unencrypted init, step 2)
    // "Old way" init (WiiBrew "Wiimote/Extension Controllers#Initializing"):
    // write 0x00 to this same 0xA400F0 register (instead of the 0x55/0x00
    // pair above) and skip 0xA400FB entirely. This leaves the extension's
    // factory-default encryption ON, so every subsequent ID/data byte read
    // from the extension must be run through DecryptExtensionByte() below.
    // Some wireless/third-party Nunchuks either ignore the "new way" write
    // or never disable encryption in the first place and only work through
    // this path - see WiiBrew's Nunchuk page, "Wireless Nunchuks" section.
    constexpr uint32_t ExtensionInitOld  = 0xA400F0;
    constexpr uint32_t ExtensionCalib    = 0xA40020; // Balance Board calibration block start
    // Reference Temperature (+1 unknown byte, always 0x01) - not part of the
    // 0xA40020 32-byte calibration block itself, but folded into that
    // block's trailing CRC32 (see WiiBrew's "Calibration Data" section: the
    // checksum covers 0x24-0x3B, then 0x20-0x21, then these two bytes at
    // 0x60-0x61).
    constexpr uint32_t ExtensionCalibRefTemp = 0xA40060;
    constexpr uint32_t ExtensionData     = 0xA40000; // live data, 11 bytes for Balance Board / 6-8 for others
    constexpr uint32_t ExtensionId       = 0xA400FA; // 6-byte extension ID (Wiimote) / 0xA400FE 2-byte (Balance Board)
    constexpr uint32_t ExtensionIdShort  = 0xA400FE; // 2-byte short form, also the "data format" byte pair
    // Balance Board "wake" register (WiiBrew's captured Wii init trace,
    // "Wii Initialisation Sequence" section): writing 0xAA here several
    // times, interspersed with reads of the calibration block, is what the
    // real Wii does before trusting the board's 4 weight sensors - skip it
    // and one or more sensors are commonly reported stuck near a constant
    // raw value (reads as ~0kg after calibration) until the next power/
    // connect cycle. Undocumented meaning; WiiBrew speculates calibration-
    // related. Present on the Balance Board only - writing it to a regular
    // Wiimote/extension is a documented no-op there.
    constexpr uint32_t BalanceBoardWake  = 0xA400F1;
    constexpr uint32_t MotionPlusBase    = 0xA60000; // - 0xA600FF
    constexpr uint32_t MotionPlusId      = 0xA600FA; // 6-byte ID, same shape as ExtensionId
    // Retail games "prime" the ID block by writing 0x55 here before every
    // activation attempt (standalone or passthrough alike). Some third-party
    // Motion Plus adapters need this step to land reliably - skipping it can
    // leave the unit in whatever mode it last activated in.
    constexpr uint32_t MotionPlusPrime   = 0xA600F0; // write 0x55, always, before MotionPlusInit
    // All three activation modes are writes to the *same* register, 0xA600FE -
    // they only differ in the byte written.
    constexpr uint32_t MotionPlusInit    = 0xA600FE; // write 0x04 (activate, "standalone" mode)
    constexpr uint32_t MotionPlusInitNunchukPass  = 0xA600FE; // write 0x05 (activate w/ Nunchuk passthrough)
    constexpr uint32_t MotionPlusInitClassicPass  = 0xA600FE; // write 0x07 (activate w/ Classic passthrough)
    constexpr uint32_t IRCameraBase      = 0xB00000; // - 0xB00033
    constexpr uint32_t IRSensitivity1    = 0xB00000; // 9-byte block
    constexpr uint32_t IRSensitivity2    = 0xB0001A; // 2-byte block
    constexpr uint32_t IRMode            = 0xB00033; // 1 byte
    constexpr uint32_t IRModeToggle      = 0xB00030; // write 0x08 (or 0x01 per "Wii" sequence variant)

    // -- Speaker registers (WiiBrew "Wiimote#Speaker_Configuration") --------
    // SpeakerInitFlag and SpeakerCommitFlag are single-byte "go" registers
    // outside the 7-byte config block itself; SpeakerConfig is where that
    // 7-byte block (format/rate/volume) gets written. See the full init
    // sequence documented on WiimoteDevice::EnableSpeaker().
    constexpr uint32_t SpeakerInitFlag   = 0xA20009; // write 0x01 here first (init step 3)
    constexpr uint32_t SpeakerConfig     = 0xA20001; // 7-byte block, 0xA20001-0xA20007 (init steps 4-5)
    constexpr uint32_t SpeakerCommitFlag = 0xA20008; // write 0x01 here last (init step 6)
}

// -- Speaker configuration -----------------------------------------------
// WiiBrew: "7 bytes control the speaker settings... the following values
// seem to produce some sound" - the exact meaning of every byte isn't
// fully reverse-engineered, only the format/rate/volume fields below are
// well-established. Byte layout of the 7-byte config block written to
// Registers::SpeakerConfig: [0]=unknown(always 0x00) [1]=format
// [2:3]=rate, little-endian [4]=volume [5:6]=unknown(always 0x00).
namespace SpeakerFormat {
    constexpr std::byte Pcm8   = 0x40_b; // signed 8-bit PCM; volume range 0x00-0xFF
    constexpr std::byte Adpcm4 = 0x00_b; // 4-bit Yamaha ADPCM; volume range 0x00-0x40
}

// rate register value = clock / desired_sample_rate_hz (WiiBrew's formula,
// integer division - exact requested rates won't always be hittable).
constexpr uint32_t kSpeakerPcmClockHz   = 12000000;
constexpr uint32_t kSpeakerAdpcmClockHz = 6000000;

// Report 0x18 (Speaker Data) carries at most this many payload bytes per
// write ("1-20 bytes may be sent at once", WiiBrew) - the report's LL
// length byte is this count shifted left 3 bits.
constexpr std::size_t kSpeakerMaxChunkBytes = 20;

// -- Extension identification ------------------------------------------------
// Read 6 bytes from Registers::ExtensionId after the "new way" init
// (write 0x55 -> 0xA400F0, then 0x00 -> 0xA400FB). These bytes come back
// UNENCRYPTED with that init method, so no decrypt step is required.
//
// If instead Registers::ExtensionInitOld was used (write 0x00 only), the
// extension's default encryption stays enabled and every byte read from
// its register space - the 6-byte ID *and* the live data bytes carried in
// normal input reports (0x32/0x34/0x35/0x36/0x37/0x3d) - comes back run
// through this transform (WiiBrew "Wiimote/Extension Controllers#The New
// Way", decrypt direction):
//   decrypted = ((encrypted ^ 0x17) + 0x17) & 0xFF
inline std::byte DecryptExtensionByte(std::byte encrypted) {
    // Arithmetic happens on the integer value; the result wraps mod 256 by
    // construction of the cast back to a byte.
    const unsigned value = (std::to_integer<unsigned>(encrypted) ^ 0x17u) + 0x17u;
    return LowByte(value);
}

inline void DecryptExtensionBytes(std::span<std::byte> data) {
    for (std::byte &b : data) b = DecryptExtensionByte(b);
}

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
struct ExtensionId6 { std::array<std::byte, 6> bytes; };

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
    if ((b[2] != 0xA4_b && b[2] != 0xA6_b) || b[3] != 0x20_b) return ExtensionType::Unknown;

    const uint16_t sub = ToU16BE(b[0], b[1]); // XXXX
    const uint16_t typ = ToU16BE(b[4], b[5]); // ZZZZ

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
    const uint16_t typ = ToU16BE(b[4], b[5]);
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
    std::array<std::byte, 9> block1;
    std::array<std::byte, 2> block2;
};

inline constexpr IRSensitivity kIRSensitivityWiiLevel3 = {
    {0x02_b, 0x00_b, 0x00_b, 0x71_b, 0x01_b, 0x00_b, 0xaa_b, 0x00_b, 0x64_b},
    {0x63_b, 0x03_b},
};

// IR data format mode numbers (written to Registers::IRMode)
namespace IRMode {
    constexpr std::byte Basic    = 0x01_b; // 10 bytes, 4 dots, X/Y only - fits report 0x37/0x36
    constexpr std::byte Extended = 0x03_b; // 12 bytes, 4 dots, X/Y + size  - fits report 0x33
    constexpr std::byte Full     = 0x05_b; // 36 bytes, 4 dots, X/Y+size+bbox+intensity - needs 0x3e/0x3f
}

} // namespace InputBridge::Wiimote
