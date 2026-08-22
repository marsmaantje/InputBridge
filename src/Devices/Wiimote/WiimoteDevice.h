// src/Devices/Wiimote/WiimoteDevice.h
//
// Owns one raw SDL_hid_device* to a Wii Remote / Wii Remote Plus / Wii
// Balance Board and drives the full report protocol directly, bypassing
// SDL_Joystick entirely, since SDL's HIDAPI Wii driver only models
// buttons/accel/rumble through SDL_Gamepad and has no representation for
// the IR camera, Nunchuk/Classic Controller/Guitar Hero extension data, or
// Balance Board weight sensors.
//
// This is deliberately NOT an SDL_Joystick / DeviceState - see
// src/Devices/Wiimote/README.md (design doc) for how this plugs into
// DeviceManager, ProtocolFieldUtils, and a new WiimoteVisualizer state.
#pragma once
#include "WiimoteProtocol.h"
#include "WiimoteState.h"
#include <SDL3/SDL_hidapi.h>
#include <cstdint>
#include <string>
#include <optional>

namespace InputBridge::Wiimote {

// Everything a caller (visualizer / protocol field mapper) needs for one
// frame. Only the fields relevant to the currently-connected extension are
// meaningful; the rest hold their default-constructed values.
struct WiimoteSnapshot {
    bool connected = false;
    std::string hid_path;
    bool is_balance_board = false; // RVL-WBC-01 vs RVL-CNT-01(-TR)

    CoreButtons core;
    AccelState  accel;
    IRState     ir;
    bool        ir_enabled = false;

    ExtensionType extension = ExtensionType::None;
    NunchukState             nunchuk;
    ClassicControllerState   classic;
    GuitarHeroState          guitar;
    BalanceBoardState        balance_board;

    BatteryBars battery = BatteryBars::Four;
    uint8_t     led_mask = 0;
    bool        rumble_on = false;

    TimestampMs last_report_ms = 0; // see TimestampMs comment in WiimoteState.h
};

class WiimoteDevice {
public:
    // Takes ownership of `dev` (already opened via SDL_hid_open_path).
    // `is_balance_board_hint` comes from the HID product string
    // ("Nintendo RVL-WBC-01") observed during enumeration.
    WiimoteDevice(SDL_hid_device *dev, std::string hid_path, bool is_balance_board_hint);
    ~WiimoteDevice();

    WiimoteDevice(const WiimoteDevice &) = delete;
    WiimoteDevice &operator=(const WiimoteDevice &) = delete;

    // Performs the startup handshake: request status, set data-reporting
    // mode, and (for Wiimotes, not Balance Boards) enable the IR camera.
    // Safe to call once after construction. Returns false on write failure
    // (device unplugged mid-init, permissions, etc).
    bool Init();

    // Drains all pending input reports and updates internal state. Call
    // once per frame from the main thread (matches SensorReader's
    // "must be called from the main thread" contract). Cheap no-op if
    // nothing is pending, since the underlying handle is non-blocking.
    void Poll();

    // Point-in-time read of everything decoded so far.
    const WiimoteSnapshot &Snapshot() const { return m_Snapshot; }

    // ── Feedback ────────────────────────────────────────────────────────
    void SetPlayerLED(int player_1to4);   // lights exactly one LED, 1-4
    void SetLEDMask(uint8_t mask4bits);   // bits 4-7, arbitrary pattern
    void SetRumble(bool on);

    // ── Low-level register access (exposed for advanced/experimental use,
    //    same rationale as WiimoteLib exposing raw read/write) ────────────
    // Synchronous: blocks (bounded, ~200ms timeout) waiting for the 0x21
    // reply. Returns false on timeout/error. `out` must have room for `size`
    // bytes (size <= 16 per WiiBrew's Read Memory and Registers Data limit
    // per packet - this helper loops internally for larger reads).
    bool ReadRegister(uint32_t address, uint16_t size, uint8_t *out);
    bool WriteRegister(uint32_t address, const uint8_t *data, uint8_t size /* <=16 */);

private:
    void HandleReport(const uint8_t *buf, int len);
    void HandleStatusReport(const uint8_t *buf);
    void HandleExtensionChanged();
    void DecodeCoreAccelIR10Ext6(const uint8_t *buf); // report 0x37, Wiimote steady-state mode
    void DecodeCoreExt19(const uint8_t *buf);          // report 0x34, Balance Board steady-state mode

    uint8_t PreferredReportMode() const;
    bool EnableIRCamera();
    bool InitExtension();       // "new way" unencrypted init + ID read
    bool LoadBalanceBoardCalibration();

    SDL_hid_device *m_Dev = nullptr;
    std::string m_Path;
    WiimoteSnapshot m_Snapshot;

    bool m_RumbleBit = false; // must be OR'd into every single output report
    std::optional<BalanceBoardCalibration> m_BalanceCal;

    // Re-request extension identification a short time after the status
    // report flags a connect/disconnect - the extension needs a moment to
    // settle before it will answer ID reads reliably.
    Uint64 m_ExtensionSettleAtMs = 0;
    bool m_ExtensionPendingInit = false;
};

} // namespace InputBridge::Wiimote
