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
    MotionPlusState          motion_plus;

    BatteryBars battery = BatteryBars::Four;
    uint8_t     led_mask = 0;
    bool        rumble_on = false;

    // Set while WiimoteDevice is actively retrying the extension-init dance
    // to clear a known Balance Board firmware quirk where one or more of
    // the 4 weight sensors report a flat 0 (see WiiBrew Wii_Balance_Board
    // "Wii Initialisation Sequence" section - this is a real hardware/
    // firmware behavior, not a decoding bug). UI can surface this so the
    // user isn't confused by weight readings jumping while it self-heals.
    bool balance_board_recovering = false;
    int  balance_board_recovery_attempts = 0;

    // True once TareBalanceBoard() has been called and its offset is being
    // subtracted from every corner reading below. Purely a runtime/software
    // zero point - it does not touch the board's own EEPROM calibration,
    // so it's safe to call as often as you like and never persists across
    // reconnects.
    bool balance_board_tared = false;

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

    // -- Feedback --------------------------------------------------------
    void SetPlayerLED(int player_1to4);   // lights exactly one LED, 1-4
    void SetLEDMask(uint8_t mask4bits);   // bits 4-7, arbitrary pattern
    void SetRumble(bool on);

    // -- Low-level register access (exposed for advanced/experimental use,
    //    same rationale as WiimoteLib exposing raw read/write) ------------
    // Synchronous: blocks (bounded, ~200ms timeout) waiting for the 0x21
    // reply. Returns false on timeout/error. `out` must have room for `size`
    // bytes (size <= 16 per WiiBrew's Read Memory and Registers Data limit
    // per packet - this helper loops internally for larger reads).
    bool ReadRegister(uint32_t address, uint16_t size, uint8_t *out);
    bool WriteRegister(uint32_t address, const uint8_t *data, uint8_t size /* <=16 */);

    // -- Balance Board tare/zero --------------------------------------------
    // Captures whatever the four corners currently read (pre-tare, i.e. the
    // board's own 0/17/34kg factory calibration applied and nothing else)
    // and stores it as a per-corner offset that gets subtracted from every
    // subsequent reading, so "current load" becomes the new zero point -
    // useful for compensating for the board's own resting weight on a rug,
    // an uneven floor, a mount, etc. Software-only: doesn't touch the
    // board's EEPROM, is lost on disconnect, and can be called again at any
    // time to re-zero (each call replaces the previous offset, it does not
    // stack). No-op if this device isn't a Balance Board or no weight
    // report has been decoded yet.
    void TareBalanceBoard();

    // Clears any offset set by TareBalanceBoard(), reverting to the board's
    // raw factory-calibrated readings.
    void ClearBalanceBoardTare();

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

    // Wii Motion Plus lives at its own register base (0xA60000) separate
    // from the regular extension port (0xA40000) and must be explicitly
    // activated before it starts feeding gyro data into the extension byte
    // slot of normal input reports. DetectMotionPlus() probes for it
    // (harmless no-op if nothing answers - a bare Wiimote or one with only
    // a Nunchuk/Classic Controller simply won't have anything at 0xA600FA);
    // ActivateMotionPlus() performs the write that switches it into
    // reporting mode, using passthrough if a Nunchuk/Classic Controller was
    // already detected on the regular extension port so both keep working
    // at once (per WiiBrew, this is the only way to get MotionPlus + an
    // extension simultaneously - the MotionPlus intercepts and re-encodes
    // the passthrough device's data alongside its own gyro bytes).
    bool DetectMotionPlus();
    bool ActivateMotionPlus();

    // Watchdog for the "one or more sensors disabled" Balance Board quirk
    // documented on WiiBrew: detects the pattern (most corners flatlined
    // while real weight is on the board) and re-runs InitExtension() to
    // try to clear it, the same fix WiiBrew describes working for their
    // PC interface. Cheap no-op for anything that isn't a Balance Board.
    void CheckBalanceBoardStuckSensors();

    // Subtracts m_BalanceTareKg from a freshly-decoded BalanceBoardState's
    // four corners in place and recomputes kg_total/cog_x/cog_y from the
    // tared values. No-op (all offsets 0) until TareBalanceBoard() is
    // called.
    void ApplyBalanceBoardTare(BalanceBoardState &bb);

    SDL_hid_device *m_Dev = nullptr;
    std::string m_Path;
    WiimoteSnapshot m_Snapshot;

    bool m_RumbleBit = false; // must be OR'd into every single output report
    std::optional<BalanceBoardCalibration> m_BalanceCal;

    bool m_MotionPlusPresent = false;   // detected at 0xA600FA
    bool m_MotionPlusActive = false;    // activation write sent + acknowledged by data arriving

    // Re-request extension identification a short time after the status
    // report flags a connect/disconnect - the extension needs a moment to
    // settle before it will answer ID reads reliably.
    Uint64 m_ExtensionSettleAtMs = 0;
    bool m_ExtensionPendingInit = false;

    // Balance Board stuck-sensor watchdog state.
    Uint64 m_BalanceStuckSinceMs = 0;          // 0 == not currently stuck
    Uint64 m_BalanceLastRecoveryAtMs = 0;      // cooldown between retries
    int    m_BalanceRecoveryAttempts = 0;      // capped so we don't spam re-init forever

    // Balance Board software tare/zero. m_BalanceRawKg holds the most
    // recent PRE-tare corner readings (factory-calibrated, offset not yet
    // applied) so TareBalanceBoard() has something to capture from;
    // m_BalanceTareKg holds the offset currently being subtracted.
    float m_BalanceRawKg[4]  = {0.f, 0.f, 0.f, 0.f}; // order: TR, BR, TL, BL
    float m_BalanceTareKg[4] = {0.f, 0.f, 0.f, 0.f};
    bool  m_BalanceHasRawReading = false;
};

} // namespace InputBridge::Wiimote