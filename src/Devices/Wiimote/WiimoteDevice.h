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
#include <vector>

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
    // True if ir_enabled is true but no IR-carrying report has been
    // decoded in longer than expected - see WiimoteDevice::TickIRWatchdog()
    // for the mechanism and why this can happen (a second process, most
    // notoriously Steam Input, silently reconfiguring the Wiimote's data
    // reporting mode out from under us). UI can use this to warn the user
    // separately from a hard "IR failed to enable" error, since the fix
    // (excluding the device from whatever else is grabbing it) is
    // different from anything wrong with our own init sequence.
    bool        ir_possibly_hijacked = false;
    // Mirrors WiimoteDevice::IsIRExtendedModeActive() - true once the
    // Extended IR toggle (SetIRExtendedMode) has actually taken effect on
    // the hardware (not just been requested), i.e. IRDot::size is live.
    // Report 0x33 carries no extension bytes at all (see
    // SetIRExtendedMode()'s comment), so nunchuk/classic/guitar below are
    // frozen at their last known values for as long as this is true - the
    // UI should treat them as stale, not as "the accessory was unplugged".
    bool        ir_extended_mode = false;

    ExtensionType extension = ExtensionType::None;
    NunchukState             nunchuk;
    ClassicControllerState   classic;
    GuitarHeroState          guitar;
    BalanceBoardState        balance_board;
    MotionPlusState          motion_plus;

    BatteryBars battery = BatteryBars::Four;
    uint8_t     led_mask = 0;

    // Target strength last passed to SetRumble(), 0..1. The physical motor
    // itself only has an on/off drive line - see SetRumble()'s comment -
    // so this is the *requested* strength, not necessarily what the motor
    // is doing at this exact instant.
    float       rumble_intensity = 0.0f;
    // Instantaneous physical state of the on/off drive line right now,
    // i.e. what UpdateRumblePWM() last actually wrote to the hardware.
    // For rumble_intensity == 0 or 1 this is constant; for anything in
    // between it flips at the ~50 Hz PWM carrier rate, so a UI polling
    // this every frame will see it toggling - that's expected, not a bug.
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

    // Sets the rumble motor's requested strength, 0.0 (off) - 1.0 (full).
    // A Wii Remote's rumble motor has no amplitude control in hardware -
    // it's a single on/off drive line (see the rumble bit OR'd into every
    // output report in WiimoteDevice.cpp) - so intermediate strengths are
    // approximated in software with PWM: Poll() rapidly switches the line
    // on and off at a nominal carrier (kRumblePwmPeriodMs, ~50 Hz) with a
    // duty cycle equal to `intensity`. That's fast enough that the motor's
    // own spin-up/spin-down inertia blurs the on/off switching into a
    // perceived amplitude change (the same trick used to dim an LED with a
    // GPIO pin that only has HIGH/LOW), rather than feeling like distinct
    // buzzes. intensity is clamped to [0,1]; exactly 0 or 1 skip PWM
    // entirely and just hold the line at the corresponding fixed state,
    // which is both the strongest possible rumble and the case that avoids
    // the extra HID write traffic PWM would otherwise add.
    //
    // UpdateRumblePWM() also carries forward a small duty-cycle debt
    // (m_RumbleDutyDebtMs) across periods that Poll() didn't get called
    // during at all - e.g. a frame hitch, or the Bluetooth stack briefly
    // stalling delivery of everything, not just rumble - so a burst of
    // missed periods reads as a slightly stronger/weaker few periods right
    // after, rather than silently wrong for however long it was missed.
    // That's the limit of what's actually correctable from here, though:
    // it compensates for OUR OWN irregular sampling of the PWM decision,
    // not for jitter in the underlying Bluetooth link itself (the delay
    // between SendReport() returning and the motor actually responding) -
    // there's no feedback path that reports when a write was really
    // delivered, so that portion of the variance is invisible to us and
    // can't be corrected in software.
    void SetRumble(float intensity);

    // Back-compat convenience for simple on/off callers.
    void SetRumble(bool on) { SetRumble(on ? 1.0f : 0.0f); }

    // -- Speaker -----------------------------------------------------------
    // Runs the full WiiBrew enable/mute/configure/unmute register sequence
    // (see WiimoteProtocol.h's Registers::SpeakerInitFlag et al.) to switch
    // the speaker into 8-bit signed PCM mode at `sample_rate_hz`. WiiBrew
    // notes 8-bit mode needs a low sample rate to keep the Bluetooth link
    // fed in time and calls out 2000Hz specifically as a good compromise -
    // that's the default here. `volume` is 0x00-0xFF, but 0xFF (max gain
    // on the hardware volume register) overdrives this speaker audibly -
    // confirmed distorted/too loud on real hardware - so the default here
    // is a conservative ~25%. Push it up if you want it louder and can
    // live with more distortion; there's no way to get more volume
    // without more distortion out of this speaker/format combination.
    // NOTE: volume=0x00 is NOT confirmed to produce true silence on real
    // hardware (WiiBrew: "the full purpose of these bytes is not known") -
    // TickSpeaker() enforces silence at volume 0 by never transmitting
    // queued audio at all, rather than relying on this register.
    // Synchronous (several WriteRegister() round-trips); call once before
    // QueuePCM8(), not every frame. Returns false if any step's HID write
    // failed.
    bool EnableSpeaker(uint32_t sample_rate_hz = 2000, uint8_t volume = 0x40);

    // Disables the speaker (Report 0x14, enable bit cleared) and discards
    // anything still queued.
    void DisableSpeaker();

    // Appends signed 8-bit PCM samples to the playback queue; does not
    // interrupt whatever's already queued/in-flight. EnableSpeaker() must
    // have been called first (and with a matching sample rate - this call
    // doesn't touch the device's rate configuration). Actual transmission
    // happens kSpeakerMaxChunkBytes at a time from Poll(), paced to the
    // rate passed to EnableSpeaker() - see TickSpeaker().
    void QueuePCM8(const int8_t *samples, size_t count);

    // True while there's still queued-but-unsent audio.
    bool IsSpeakerPlaying() const { return m_SpeakerQueuePos < m_SpeakerQueue.size(); }

    // Discards queued-but-unsent audio without disabling the speaker
    // itself (use DisableSpeaker() for that).
    void StopSpeaker();

    // Convenience wrapper for testing/notification sounds: calls
    // EnableSpeaker() (if not already enabled at a matching rate) and
    // queues a generated sine-wave tone. Not meant for anything beyond
    // simple beeps - for real audio, generate/decode your own PCM8 buffer
    // and use QueuePCM8() directly.
    void PlayBeep(float freq_hz = 440.0f, uint32_t duration_ms = 200,
                   uint32_t sample_rate_hz = 2000, uint8_t volume = 0x40);

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

    // -- IR Extended mode toggle ---------------------------------------------
    // Switches between IR Basic mode (report 0x37: X/Y only, but leaves room
    // for Nunchuk/Classic/Guitar extension data in the same report) and IR
    // Extended mode (report 0x33: X/Y + a 4-bit dot size, but the hardware's
    // report format for 0x33 has NO extension bytes at all - WiiBrew doesn't
    // offer a report that combines size data with extension data, and Full
    // mode's IR+brightness data is a separate interleaved 0x3e/0x3f pair,
    // not covered here). While Extended mode is active, nunchuk/classic/
    // guitar in the snapshot are simply frozen at whatever they last held -
    // check ir_extended_mode before trusting them as live.
    //
    // Re-programs the running Wiimote synchronously: re-sends the IR mode
    // register write (part of the same WiiBrew init sequence used at
    // connect time - see EnableIRCameraOnce()) and the data reporting mode.
    // Like EnableIRCameraOnce(), this sleeps briefly between writes
    // (kIRInitStepDelayMs per step), so calling it is a deliberate,
    // user-initiated action (e.g. a settings toggle) rather than something
    // to call from a hot path. No-op if already in the requested mode, or
    // if this device is a Balance Board (no IR/camera hardware).
    bool SetIRExtendedMode(bool enabled);
    bool IsIRExtendedModeActive() const { return m_Snapshot.ir_extended_mode; }

private:
    void HandleReport(const uint8_t *buf, int len);
    void HandleStatusReport(const uint8_t *buf);
    void HandleExtensionChanged();
    void DecodeCoreAccelIR10Ext6(const uint8_t *buf); // report 0x37, Wiimote steady-state mode
    void DecodeCoreAccelIR12(const uint8_t *buf);      // report 0x33, Extended IR mode (adds dot size)
    void DecodeCoreExt19(const uint8_t *buf);          // report 0x34, Balance Board steady-state mode

    uint8_t PreferredReportMode() const;

    // EnableIRCamera() runs EnableIRCameraOnce() + VerifyIRCameraEnabled()
    // in a bounded retry loop - WiiBrew documents the raw init sequence as
    // landing in a working state only "pretty much random[ly]" even with
    // correct inter-write delays, and explicitly recommends repeating the
    // whole sequence rather than trusting a single pass. EnableIRCameraOnce()
    // is one attempt at the 7-step WiiBrew sequence, with the recommended
    // >=50ms delay enforced between every write. VerifyIRCameraEnabled()
    // confirms the attempt actually worked by requesting a fresh status
    // report and checking its "IR camera enabled" bit, rather than treating
    // "every HID write returned success" as proof the camera is producing
    // data.
    bool EnableIRCamera();
    bool EnableIRCameraOnce();
    bool VerifyIRCameraEnabled();

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

    // Watchdog for a second process (in practice, almost always Steam
    // Input - see TickIRWatchdog()'s comment for why) silently changing
    // the Wiimote's data reporting mode after we've successfully enabled
    // and configured the IR camera. Detects the resulting "IR data just
    // stops arriving" pattern and re-asserts our mode; also flags
    // ir_possibly_hijacked so the UI can tell the user this isn't the same
    // failure as EnableIRCamera() itself failing.
    void TickIRWatchdog();

    // Drives the software-PWM approximation described on SetRumble(float).
    // Called once per Poll() (so at whatever cadence DeviceManager polls
    // Wiimotes at - see WiimoteDevice.cpp for why that's fine even though
    // it's not a hard real-time scheduler) and also once immediately from
    // SetRumble() itself so a new intensity takes effect right away rather
    // than waiting up to one Poll() tick. Only writes to the device when
    // the on/off line actually needs to change state, not every call.
    void UpdateRumblePWM();

    // Drains m_SpeakerQueue into Report 0x18 writes, kSpeakerMaxChunkBytes
    // at a time, no faster than m_SpeakerChunkIntervalMs apart (computed in
    // EnableSpeaker() from the requested sample rate). Called once per
    // Poll(), same cadence rationale as UpdateRumblePWM(). No-op when the
    // speaker isn't enabled or the queue is empty.
    void TickSpeaker();

    // Subtracts m_BalanceTareKg from a freshly-decoded BalanceBoardState's
    // four corners in place and recomputes kg_total/cog_x/cog_y from the
    // tared values. No-op (all offsets 0) until TareBalanceBoard() is
    // called.
    void ApplyBalanceBoardTare(BalanceBoardState &bb);

    SDL_hid_device *m_Dev = nullptr;
    std::string m_Path;
    WiimoteSnapshot m_Snapshot;

    bool m_RumbleBit = false; // must be OR'd into every single output report

    // Software-PWM rumble state (see SetRumble(float) / UpdateRumblePWM()).
    float m_RumbleIntensity = 0.0f;   // target strength, 0..1, set by SetRumble()
    Uint64 m_RumbleCycleStartMs = 0;  // start of the current PWM period, reset on every SetRumble() call
    static constexpr Uint64 kRumblePwmPeriodMs = 20; // ~50 Hz nominal carrier, see SetRumble(float)

    // Duty-cycle debt carried across periods Poll() didn't run during at
    // all (see UpdateRumblePWM()) - ms of "on" time owed (positive) or
    // over-delivered (negative), paid back by nudging later periods'
    // on/off boundary instead of the target intensity itself. Reset
    // alongside m_RumbleCycleStartMs on every SetRumble() call so a new
    // target doesn't inherit the previous one's leftover correction.
    float  m_RumbleDutyDebtMs = 0.0f;
    // Last time UpdateRumblePWM() actually ran, independent of
    // m_RumbleCycleStartMs (which only moves on SetRumble()) - the gap
    // between this and "now" on each call is what reveals a skipped
    // period in the first place.
    Uint64 m_RumbleLastPollMs = 0;

    std::optional<BalanceBoardCalibration> m_BalanceCal;

    // Speaker playback queue (see EnableSpeaker()/QueuePCM8()/TickSpeaker()).
    bool   m_SpeakerEnabled = false;
    uint32_t m_SpeakerSampleRateHz = 0; // rate last passed to EnableSpeaker(), 0 = never enabled
    uint8_t  m_SpeakerVolume = 0;       // volume last passed to EnableSpeaker() (see PlayBeep())
    std::vector<int8_t> m_SpeakerQueue;
    size_t m_SpeakerQueuePos = 0;
    Uint64 m_SpeakerNextChunkAtMs = 0;
    Uint32 m_SpeakerChunkIntervalMs = 10; // recomputed by EnableSpeaker() from sample_rate_hz

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

    // IR-hijack watchdog state (see TickIRWatchdog()). Tracks the last
    // time an IR-carrying report (0x37 or 0x33) was actually decoded,
    // separately from last_report_ms (which updates for ANY report,
    // including ones a competing process's reconfiguration switched us to).
    Uint64 m_LastIRReportMs = 0;
    Uint64 m_LastIRReassertAtMs = 0;    // cooldown between corrective re-sends
    int    m_IRReassertAttempts = 0;    // capped, same rationale as balance board recovery

    // IR Extended mode toggle (see SetIRExtendedMode()). This is the single
    // source of truth PreferredReportMode() and EnableIRCameraOnce() read
    // to decide between report 0x37/IRMode::Basic and report 0x33/
    // IRMode::Extended; m_Snapshot.ir_extended_mode mirrors it only once
    // the switch has actually been re-programmed on the hardware.
    bool m_IRExtendedMode = false;

    // Balance Board software tare/zero. m_BalanceRawKg holds the most
    // recent PRE-tare corner readings (factory-calibrated, offset not yet
    // applied) so TareBalanceBoard() has something to capture from;
    // m_BalanceTareKg holds the offset currently being subtracted.
    float m_BalanceRawKg[4]  = {0.f, 0.f, 0.f, 0.f}; // order: TR, BR, TL, BL
    float m_BalanceTareKg[4] = {0.f, 0.f, 0.f, 0.f};
    bool  m_BalanceHasRawReading = false;
};

} // namespace InputBridge::Wiimote
