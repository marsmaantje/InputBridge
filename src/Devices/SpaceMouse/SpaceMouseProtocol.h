// src/Devices/SpaceMouse/SpaceMouseProtocol.h
//
// Constants, the known-model table, and the pure (no SDL, no I/O) report
// decoder for 3Dconnexion 6-DOF devices (SpaceNavigator, SpaceMouse, ...).
// Kept free of any SDL dependency so it can be unit tested in isolation.
//
// The report layout is the common 3Dconnexion HID protocol also documented
// by OpenNDOF (https://github.com/bpmcgill/OpenNDOF):
//
//   ID 0x01  translation  TX,TY,TZ  int16 little-endian   (7 bytes)
//   ID 0x02  rotation     RX,RY,RZ  int16 little-endian   (7 bytes)
//   ID 0x03  buttons      bitmask, 1 bit per button, LSB first
//
// Two additions beyond that table, both purely defensive (a device that
// never sends them is unaffected):
//   ID 0x01 with >= 13 bytes  -> combined TX..RZ report (some newer firmware)
//   ID 0x17                   -> battery percent (wireless models)
//
// This code is strictly READ-ONLY with respect to the hardware: nothing in
// the SpaceMouse module ever writes an output/feature report (LEDs, mode
// switches, LCD, ...), which is what lets it coexist with 3DxWare/spacenavd.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace InputBridge::SpaceMouse {

constexpr uint16_t kVendorLogitech    = 0x046D; // legacy 3Dconnexion devices
constexpr uint16_t kVendor3Dconnexion = 0x256F;

// HID usage of every 3Dconnexion motion interface: Generic Desktop /
// Multi-axis Controller.
constexpr uint16_t kUsagePageGenericDesktop = 0x01;
constexpr uint16_t kUsageMultiAxisController = 0x08;

constexpr int kNumMotionAxes = 6;
constexpr int kMaxButtons    = 32;

// Raw deflection at full travel. A SpaceNavigator tops out around +/-350;
// values beyond are clamped when normalising.
constexpr float kDefaultFullScale = 350.0f;

struct ModelInfo {
    uint16_t    vendor_id;
    uint16_t    product_id;
    const char *name;     // short model name, e.g. "SpaceNavigator"
    int         buttons;  // number of physical buttons (<= kMaxButtons)
};

// Known models. NOTE: PIDs are from public sources (OpenNDOF, spacenavd,
// the Linux hid-3dconnexion driver) and have not all been verified against
// hardware. Unknown 3Dconnexion-vendor multi-axis devices are still picked
// up via IdentifyDevice()'s usage-based path.
const ModelInfo *FindKnownModel(uint16_t vid, uint16_t pid);
// Lookup by short model name ("SpaceNavigator"); nullptr if unknown.
const ModelInfo *FindModelByName(const std::string &name);

// Decides whether a HID interface is a 3D mouse motion interface.
// `usage_page`/`usage` are 0 when the HID backend could not report them.
//  - known VID/PID + (usage unknown or Multi-axis)         -> table entry
//  - 3Dconnexion VID + Multi-axis usage, PID not in table  -> generic entry
//  - anything else                                         -> nullopt
// Deliberately never matches arbitrary Logitech devices.
std::optional<ModelInfo> IdentifyDevice(uint16_t vid, uint16_t pid,
                                        uint16_t usage_page, uint16_t usage);

// Label for a raw button bit; "Button N" when the model has no known names.
std::string ButtonName(const ModelInfo &model, int button);

// -- Decoder -----------------------------------------------------------------
struct State {
    int16_t  raw[kNumMotionAxes] = {};   // TX,TY,TZ,RX,RY,RZ as reported
    uint32_t buttons             = 0;    // bit n == button n pressed
    int      battery_percent     = -1;   // -1 unknown
};

enum class ReportKind { Ignored, Translation, Rotation, Combined, Buttons, Battery };

// Decodes one input report (buf[0] == report ID) into `state`.
// Malformed / unknown reports return Ignored and leave `state` untouched.
ReportKind DecodeReport(const uint8_t *buf, size_t len, State &state);

// raw -> [-1, 1], clamped.
float NormalizeAxis(int16_t raw, float full_scale = kDefaultFullScale);

} // namespace InputBridge::SpaceMouse
