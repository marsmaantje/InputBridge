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
#include "WiimoteADPCM.h"
#include "WiimoteProtocol.h"
#include "WiimoteState.h"
#include "WiimoteTransport.h"
#include <SDL3/SDL_stdinc.h>
#include <cstdint>
#include <string>
#include <optional>
#include <memory>
#include <vector>

namespace InputBridge::Wiimote {

// Which speaker encoding (see WiimoteProtocol.h's SpeakerFormat namespace
// for the raw register values) EnableSpeaker() configures the hardware
// for. Type-safe wrapper so callers can't pass an unrelated byte.
enum class SpeakerAudioFormat : uint8_t {
    PCM8,   // signed 8-bit linear PCM - simple, but needs a low sample rate
            // to keep the Bluetooth link fed, hence the thin/aliased sound.
    ADPCM4, // 4-bit Yamaha ADPCM (WiimoteADPCM.h) - ~double the usable
            // sample rate for the same data rate; fixes the above.
};

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
    // True if ir_enabled but no IR-carrying report has decoded in longer
    // than expected - see TickIRWatchdog(). Usually means a second process
    // (typically Steam Input) silently reconfigured the reporting mode
    // out from under us; the fix differs from an init failure, hence the
    // separate flag.
    bool        ir_possibly_hijacked = false;
    // Mirrors IsIRExtendedModeActive(): true once the Extended IR toggle
    // has actually taken effect on hardware. Report 0x33 carries no
    // extension bytes, so nunchuk/classic/guitar below stay frozen at
    // their last values while this is true - treat as stale, not
    // "unplugged".
    bool        ir_extended_mode = false;

    ExtensionType extension = ExtensionType::None;
    // True if InitExtension() had to fall back to the "old way" (encrypted)
    // init - see WiimoteProtocol.h's DecryptExtensionByte(). Informational
    // only; decoding already accounts for it.
    bool          extension_encrypted = false;
    NunchukState             nunchuk;
    ClassicControllerState   classic;
    GuitarHeroState          guitar;
    BalanceBoardState        balance_board;
    MotionPlusState          motion_plus;

    BatteryBars battery = BatteryBars::Four;
    uint8_t     led_mask = 0;

    // Requested strength last passed to SetRumble(), 0..1 - the motor
    // itself is on/off only (see SetRumble()), so this isn't necessarily
    // what the motor is doing this instant.
    float       rumble_intensity = 0.0f;
    // Instantaneous physical state of the on/off drive line, i.e. what
    // UpdateRumblePWM() last wrote. Constant at intensity 0/1; flips at
    // the ~50Hz PWM rate for anything in between - expected, not a bug.
    bool        rumble_on = false;

    // Set while retrying the extension-init dance to clear a known Balance
    // Board firmware quirk (one or more of the 4 sensors reporting a flat
    // 0 - real hardware behavior per WiiBrew, not a decode bug).
    bool balance_board_recovering = false;
    int  balance_board_recovery_attempts = 0;

    // True once TareBalanceBoard() has set an offset being subtracted from
    // every corner reading. Software-only zero point: doesn't touch the
    // board's EEPROM and never persists across reconnects on its own.
    bool balance_board_tared = false;

    TimestampMs last_report_ms = 0; // see TimestampMs comment in WiimoteState.h
};

class WiimoteDevice {
public:
    // Takes ownership of `transport` (already open/connected - either a
    // WiimoteHidTransport or, on Linux, a WiimoteL2CAPTransport - see
    // WiimoteTransport.h). `is_balance_board_hint` comes from the HID
    // product string ("Nintendo RVL-WBC-01") seen during enumeration.
    WiimoteDevice(std::unique_ptr<IWiimoteTransport> transport, std::string hid_path, bool is_balance_board_hint);
    ~WiimoteDevice();

    WiimoteDevice(const WiimoteDevice &) = delete;
    WiimoteDevice &operator=(const WiimoteDevice &) = delete;

    // Startup handshake: request status, set data-reporting mode, and (for
    // Wiimotes, not Balance Boards) enable the IR camera. Safe to call
    // once after construction. Returns false on write failure.
    bool Init();

    // Drains pending input reports and updates internal state. Call once
    // per frame from the main thread. Cheap no-op if nothing is pending.
    void Poll();

    // Point-in-time read of everything decoded so far.
    const WiimoteSnapshot &Snapshot() const { return m_Snapshot; }

    // One-shot latch for "have saved preferences (player LED, IR Extended
    // mode, Balance Board tare - see PreferencesManager) been restored onto
    // this device yet". Tracked here rather than via
    // PreferencesManager::IsPreferenceApplied since that's keyed by
    // SDL_JoystickID and WiimoteDevice has none. Public and unguarded,
    // same check-then-set pattern as the SDL-backed device settings tab.
    bool prefs_applied = false;

    // -- Feedback --------------------------------------------------------
    void SetPlayerLED(int player_1to4);   // lights exactly one LED, 1-4
    void SetLEDMask(uint8_t mask4bits);   // bits 4-7, arbitrary pattern

    // Sets the rumble motor's requested strength, 0.0 (off) - 1.0 (full).
    // The motor has no hardware amplitude control - just an on/off drive
    // line OR'd into every output report - so intermediate strengths are
    // approximated via software PWM: Poll() switches the line at a
    // nominal ~50Hz carrier (kRumblePwmPeriodMs) with duty cycle equal to
    // `intensity`. Fast enough that the motor's own spin-up/down inertia
    // blurs it into a perceived amplitude change. 0 or 1 skip PWM
    // entirely (fixed line state, less HID traffic).
    //
    // UpdateRumblePWM() carries forward a small duty-cycle debt
    // (m_RumbleDutyDebtMs) across periods Poll() didn't run during (a
    // frame hitch, a Bluetooth stall), so a burst of missed periods reads
    // as slightly over/under-strength shortly after rather than silently
    // wrong. This only compensates for our own irregular sampling, not
    // for Bluetooth delivery jitter itself - there's no feedback path for
    // when a write actually reaches the motor.
    void SetRumble(float intensity);

    // Back-compat convenience for simple on/off callers.
    void SetRumble(bool on) { SetRumble(on ? 1.0f : 0.0f); }

    // -- Speaker -----------------------------------------------------------
    // Runs the WiiBrew enable/mute/configure/unmute register sequence to
    // switch the speaker into `format` at `sample_rate_hz`. `format`
    // defaults to PCM8 for back-compat; pass ADPCM4 (with QueueADPCM4())
    // for better quality. WiiBrew suggests 2000Hz for 8-bit mode and
    // 3000Hz as its "standard value" for 4-bit mode - the default here
    // (2000Hz) only matches the former, so ADPCM4 callers will usually
    // want to pass a higher rate explicitly (PlayBeep() does this).
    // `volume` is 0x00-0xFF in PCM8 but only 0x00-0x40 in ADPCM4 (WiiBrew);
    // out-of-range values are clamped rather than writing an undocumented
    // register value. The hardware's own max gain audibly overdrives this
    // speaker, so the default here is a conservative ~25% of PCM8's range
    // - push higher for more volume at the cost of more distortion.
    // NOTE: volume=0x00 is not confirmed to mean true silence on real
    // hardware, so TickSpeaker() enforces silence at volume 0 by never
    // transmitting queued audio, rather than relying on the register.
    // Synchronous (several WriteRegister() round-trips); call once before
    // queuing audio, not every frame. Returns false if any step's HID
    // write failed. Switching `format` from what was active before
    // discards anything still queued (see QueueADPCM4()'s comment on why
    // mixing formats mid-queue can't work) - including via
    // DisableSpeaker() -> EnableSpeaker() with a different format.
    bool EnableSpeaker(uint32_t sample_rate_hz = 2000, uint8_t volume = 0x40,
                        SpeakerAudioFormat format = SpeakerAudioFormat::PCM8);

    // Disables the speaker (Report 0x14, enable bit cleared) and discards
    // anything still queued.
    void DisableSpeaker();

    // Appends signed 8-bit PCM samples to the playback queue without
    // interrupting what's already queued/in-flight. EnableSpeaker() must
    // already be active in PCM8 format at a matching sample rate (this
    // call doesn't touch rate configuration). Sent kSpeakerMaxChunkBytes
    // at a time from Poll(), paced to that rate - see TickSpeaker().
    void QueuePCM8(const int8_t *samples, size_t count);

    // Encodes `count` signed 16-bit PCM samples to 4-bit Yamaha ADPCM (see
    // WiimoteADPCM.h) and appends the result to the playback queue.
    // EnableSpeaker() must already be active in ADPCM4 format (same
    // rate-configuration contract as QueuePCM8()).
    //
    // The encoder's predictor/step state (and any odd leftover nibble)
    // carries over between calls so back-to-back calls encode one
    // continuous stream - each ADPCM nibble only makes sense relative to
    // the decoder's running prediction, unlike standalone 8-bit PCM
    // samples, so restarting cold would desync from the Wiimote's onboard
    // decoder. This state resets on EnableSpeaker()'s reconfigure and
    // StopSpeaker()'s discard (both already leave the hardware decoder's
    // state stale anyway), so expect a small audible discontinuity right
    // at that boundary - the same "garbled squawk on reconnect" a live
    // EnableSpeaker() reconfigure already causes.
    void QueueADPCM4(const int16_t *samples, size_t count);

    // True while there's still queued-but-unsent audio.
    bool IsSpeakerPlaying() const { return m_SpeakerQueuePos < m_SpeakerQueue.size(); }

    // Discards queued-but-unsent audio without disabling the speaker (use
    // DisableSpeaker() for that). Also resets the ADPCM4 encoder state
    // (harmless no-op in PCM8 mode) since a discard implies the same
    // desync QueueADPCM4() guards against.
    void StopSpeaker();

    // Convenience wrapper for testing/notification sounds: calls
    // EnableSpeaker() (if not already active at matching settings) and
    // queues a generated sine tone in `format` (ADPCM4 by default, for the
    // same quality reason as SpeakerAudioFormat). Not meant for real
    // audio - use QueuePCM8()/QueueADPCM4() directly for that.
    // `sample_rate_hz` of 0 (default) picks WiiBrew's suggested rate for
    // whichever `format` was requested (2000Hz PCM8 / 3000Hz ADPCM4)
    // rather than one literal default only well-tuned for one format.
    void PlayBeep(float freq_hz = 440.0f, uint32_t duration_ms = 200,
                   uint32_t sample_rate_hz = 0, uint8_t volume = 0x40,
                   SpeakerAudioFormat format = SpeakerAudioFormat::ADPCM4);

    // -- Low-level register access (exposed for advanced/experimental use,
    //    same rationale as WiimoteLib exposing raw read/write) ------------
    // Synchronous: blocks (bounded, ~200ms timeout) waiting for the 0x21
    // reply. Returns false on timeout/error. `out` needs room for `size`
    // bytes (this helper loops internally past WiiBrew's 16-byte-per-
    // packet read limit).
    bool ReadRegister(uint32_t address, uint16_t size, uint8_t *out);
    bool WriteRegister(uint32_t address, const uint8_t *data, uint8_t size /* <=16 */);

    // -- Balance Board tare/zero --------------------------------------------
    // Captures the four corners' current (pre-tare, factory-calibrated)
    // readings and stores them as a per-corner offset subtracted from
    // every subsequent reading, so "current load" becomes the new zero -
    // useful for a rug, uneven floor, or mount under the board.
    // Software-only: doesn't touch the board's EEPROM, is lost on
    // disconnect, and can be re-called anytime (replaces, doesn't stack).
    // No-op if this isn't a Balance Board or nothing's been decoded yet.
    void TareBalanceBoard();

    // Clears any offset set by TareBalanceBoard(), reverting to the
    // board's raw factory-calibrated readings.
    void ClearBalanceBoardTare();

    // Directly sets the per-corner tare offset (order: TR, BR, TL, BL),
    // bypassing TareBalanceBoard()'s "capture the current reading"
    // behavior - lets a previously-saved tare
    // (PreferencesManager::GetWiimoteBalanceTareKg) be restored on
    // reconnect before a fresh weight report arrives. All-zero is
    // equivalent to ClearBalanceBoardTare().
    void SetBalanceBoardTareValues(float top_right, float bottom_right, float top_left, float bottom_left);

    // Point-in-time read of the offset currently subtracted from every
    // corner reading, same TR/BR/TL/BL order as
    // SetBalanceBoardTareValues() - for persisting the current tare.
    void GetBalanceBoardTareValues(float outKg[4]) const;

    // -- IR Extended mode toggle ---------------------------------------------
    // Switches between IR Basic mode (report 0x37: X/Y, with room left for
    // extension data in the same report) and IR Extended mode (report
    // 0x33: X/Y + a 4-bit dot size, but no extension bytes at all - WiiBrew
    // has no report combining size with extension data; Full mode's
    // separate 0x3e/0x3f pair isn't covered here). While Extended mode is
    // active, nunchuk/classic/guitar in the snapshot stay frozen at their
    // last values - check ir_extended_mode before trusting them.
    //
    // Re-programs the running Wiimote synchronously (same IR mode register
    // write used at connect time, plus the data reporting mode) - sleeps
    // briefly between writes like EnableIRCameraOnce(), so treat this as a
    // deliberate user action (a settings toggle), not a hot-path call.
    // No-op if already in the requested mode or on a Balance Board.
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
    // correct delays, and recommends repeating the whole sequence rather
    // than trusting one pass. EnableIRCameraOnce() is one attempt at the
    // 7-step sequence with the recommended >=50ms inter-write delay.
    // VerifyIRCameraEnabled() confirms it actually worked via a fresh
    // status report's "IR camera enabled" bit, not just "writes succeeded".
    bool EnableIRCamera();
    bool EnableIRCameraOnce();
    bool VerifyIRCameraEnabled();

    // Tries the "new way" (unencrypted) init first; if the resulting ID
    // doesn't decode to a recognizable extension, falls back to the "old
    // way" (encrypted) init, setting m_ExtensionEncrypted /
    // WiimoteSnapshot::extension_encrypted so decoding knows to decrypt
    // live data bytes too. See WiimoteProtocol.h.
    bool InitExtension();
    // The unencrypted ("new way") encryption-reset write sequence
    // InitExtension() begins with, factored out so ActivateMotionPlus()
    // can redo just this part before writing a passthrough-activation
    // register, without recursing into the rest of InitExtension().
    bool ResetExtensionEncryption();
    bool LoadBalanceBoardCalibration();

    // Wii Motion Plus lives at its own register base (0xA60000), separate
    // from the regular extension port (0xA40000), and must be explicitly
    // activated before it feeds gyro data into normal input reports'
    // extension byte slot. DetectMotionPlus() probes for it (harmless
    // no-op if nothing answers at 0xA600FA). ActivateMotionPlus() sends
    // the activation write, using passthrough if a Nunchuk/Classic
    // Controller was already on the regular port so both keep working -
    // per WiiBrew this is the only way to run MotionPlus + an extension
    // simultaneously (MotionPlus intercepts and re-encodes the
    // passthrough device's data alongside its own gyro bytes).
    bool DetectMotionPlus();
    bool ActivateMotionPlus();

    // Watchdog for WiiBrew's documented "one or more sensors disabled"
    // Balance Board quirk: detects the pattern (most corners flatlined
    // while real weight is on the board) and re-runs InitExtension(),
    // same fix WiiBrew describes for their PC interface. No-op for
    // anything that isn't a Balance Board.
    void CheckBalanceBoardStuckSensors();

    // Watchdog for a second process (almost always Steam Input) silently
    // changing the Wiimote's reporting mode after we've enabled/configured
    // IR. Detects "IR data just stops arriving" and re-asserts our mode;
    // also flags ir_possibly_hijacked so the UI can distinguish this from
    // EnableIRCamera() itself failing.
    void TickIRWatchdog();

    // Drives the software-PWM approximation described on SetRumble(float).
    // Called once per Poll() and once immediately from SetRumble() itself
    // so a new intensity takes effect right away. Only writes to the
    // device when the on/off line actually needs to change.
    void UpdateRumblePWM();

    // Drains m_SpeakerQueue into Report 0x18 writes, kSpeakerMaxChunkBytes
    // at a time, no faster than m_SpeakerChunkIntervalMs apart (computed
    // in EnableSpeaker() from the requested rate). Called once per Poll().
    // No-op when the speaker isn't enabled or the queue is empty.
    void TickSpeaker();

    // Subtracts m_BalanceTareKg from a freshly-decoded BalanceBoardState's
    // four corners in place and recomputes kg_total/cog_x/cog_y. No-op
    // (all offsets 0) until TareBalanceBoard() is called.
    void ApplyBalanceBoardTare(BalanceBoardState &bb);

    std::unique_ptr<IWiimoteTransport> m_Transport;
    std::string m_Path;
    WiimoteSnapshot m_Snapshot;

    bool m_RumbleBit = false; // must be OR'd into every single output report

    // Software-PWM rumble state (see SetRumble(float) / UpdateRumblePWM()).
    float m_RumbleIntensity = 0.0f;   // target strength, 0..1, set by SetRumble()
    Uint64 m_RumbleCycleStartMs = 0;  // start of the current PWM period, reset on every SetRumble() call
    static constexpr Uint64 kRumblePwmPeriodMs = 20; // ~50 Hz nominal carrier, see SetRumble(float)

    // Duty-cycle debt carried across periods Poll() didn't run during (see
    // UpdateRumblePWM()) - ms of "on" time owed (positive) or
    // over-delivered (negative), paid back by nudging later on/off
    // boundaries rather than the target intensity. Reset with
    // m_RumbleCycleStartMs on every SetRumble() call.
    float  m_RumbleDutyDebtMs = 0.0f;
    // Last time UpdateRumblePWM() actually ran, independent of
    // m_RumbleCycleStartMs (which only moves on SetRumble()) - the gap to
    // "now" on each call is what reveals a skipped period.
    Uint64 m_RumbleLastPollMs = 0;

    std::optional<BalanceBoardCalibration> m_BalanceCal;

    // Speaker playback queue (see EnableSpeaker()/QueuePCM8()/
    // QueueADPCM4()/TickSpeaker()). Bytes here are already in whatever
    // format m_SpeakerFormat says - TickSpeaker() just streams them out.
    bool   m_SpeakerEnabled = false;
    uint32_t m_SpeakerSampleRateHz = 0; // rate last passed to EnableSpeaker(), 0 = never enabled
    uint8_t  m_SpeakerVolume = 0;       // volume last passed to EnableSpeaker() (see PlayBeep())
    SpeakerAudioFormat m_SpeakerFormat = SpeakerAudioFormat::PCM8; // format last passed to EnableSpeaker()
    std::vector<int8_t> m_SpeakerQueue;
    size_t m_SpeakerQueuePos = 0;
    Uint64 m_SpeakerNextChunkAtMs = 0;
    Uint32 m_SpeakerChunkIntervalMs = 10; // recomputed by EnableSpeaker() from sample_rate_hz/format

    // Running Yamaha ADPCM encoder state for QueueADPCM4() - persists
    // across calls (see that method's comment); reset in
    // EnableSpeaker()/StopSpeaker().
    YamahaAdpcm4Encoder m_AdpcmEncoder;

    bool m_MotionPlusPresent = false;   // detected at 0xA600FA
    bool m_MotionPlusActive = false;    // activation write sent + acknowledged by data arriving

    // A Motion Plus that ISN'T behind a Nunchuk/Classic Controller (i.e.
    // the common case: a bare external Motion Plus, or a Wii Remote Plus's
    // built-in one) never toggles the status report's regular extension-
    // connected bit (byte 3, 0x02) - per WiiBrew, that bit only reflects
    // the 0xA40000 port, which a not-yet-activated Motion Plus doesn't
    // occupy. HandleExtensionChanged() (and therefore InitExtension()/
    // DetectMotionPlus()) is gated entirely on that bit changing, so
    // without this timer a bare Motion Plus would simply never be probed
    // for. Set to a real deadline once Init() completes; Poll() then
    // calls DetectMotionPlus() directly (not the heavier InitExtension())
    // once that deadline passes and re-arms it for ~8s later - WiiBrew's
    // own recommended re-poll interval for an inactive Motion Plus -
    // until one is actually found (m_MotionPlusPresent stops the retries).
    Uint64 m_MotionPlusNextProbeAtMs = 0;

    // Re-request extension identification a short time after the status
    // report flags a connect/disconnect - the extension needs a moment to
    // settle before it answers ID reads reliably.
    Uint64 m_ExtensionSettleAtMs = 0;
    bool m_ExtensionPendingInit = false;

    // A single post-settle identification attempt isn't always enough -
    // slower/third-party Nunchuks can still be powering up at the 150ms
    // mark and answer the ID read with garbage, classifying as Unknown.
    // Without a retry that sticks until the cable is pulled and reinserted
    // (the only thing that re-arms m_ExtensionSettleAtMs). Poll() retries
    // roughly once a second, using this deadline, for as long as the port
    // is connected but still unclassified; a successful classification
    // moves m_Snapshot.extension off Unknown/None and the retries stop.
    Uint64 m_ExtensionRetryAtMs = 0;

    // Tracks the physical extension-port connected bit (status report byte
    // 3, 0x02) as of the last status report, independent of whether
    // InitExtension() has run yet. HandleStatusReport() diffs the incoming
    // bit against THIS, not m_Snapshot.extension - using
    // "m_Snapshot.extension != None" as the "already connected" check
    // self-retriggers for as long as any status report (ours or a
    // competing process's) arrives before the 150ms settle window closes,
    // which can starve InitExtension()/DetectMotionPlus() of ever running.
    bool m_ExtensionPortConnected = false;

    // Once a Motion Plus is present, the status report's extension-
    // connected bit (m_ExtensionPortConnected above) stops being
    // trustworthy for "did the passthrough device behind it change" -
    // WiiBrew notes this bit gets flaky once a Motion Plus occupies the
    // port, and it can flip on essentially any status report while
    // nothing physically changed. Treating every flip as authoritative
    // used to tear down/re-detect the whole extension+MotionPlus state via
    // HandleExtensionChanged() each time, producing a "jumps in and out of
    // active" symptom from a single spurious status reply.
    //
    // Once m_MotionPlusPresent is true, HandleStatusReport() stops acting
    // on m_ExtensionPortConnected and defers to THIS instead: the Motion
    // Plus's own passthrough data carries its own extension_connected bit
    // (ext[4] bit 0 - see Decode::MotionPlus), reflecting what's actually
    // behind it right now. -1 = not yet observed (don't trigger on the
    // first reading, just record a baseline); 0/1 once real data arrives.
    int8_t m_MotionPlusExtConnected = -1;

    // Same settle rationale as m_ExtensionSettleAtMs, but for the initial
    // handshake (Init(), especially EnableIRCamera()). A successfully-
    // constructed transport only means the node/socket is openable - on
    // Bluetooth it doesn't guarantee the HID control/interrupt channels
    // have finished negotiating, and writes into that window can be
    // silently swallowed even though Write() reports success. Invisible
    // for a Wiimote already on before InputBridge started (connection is
    // old news by the time Scan() opens it); reproduces reliably when the
    // Wiimote is turned on while InputBridge is already running, since the
    // open-to-Init() gap is then just milliseconds. Deferring Init() to
    // run from Poll() once m_InitSettleAtMs has passed (set at
    // construction) fixes this without touching the separate
    // kIRInitStepDelayMs spacing or verify-and-retry loop already in
    // EnableIRCamera(), which handle WiiBrew's unrelated "random" init
    // flakiness.
    Uint64 m_InitSettleAtMs = 0;
    bool m_InitPending = false;

    // Mirrors WiimoteSnapshot::extension_encrypted - set by InitExtension()
    // when the "old way" fallback was needed, consulted by
    // DecodeCoreAccelIR10Ext6() to decrypt live extension data bytes.
    bool m_ExtensionEncrypted = false;

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

    // IR Extended mode toggle (see SetIRExtendedMode()) - the source of
    // truth PreferredReportMode()/EnableIRCameraOnce() read to pick report
    // 0x37/IRMode::Basic vs. 0x33/IRMode::Extended. m_Snapshot.ir_extended_mode
    // mirrors it only once the hardware switch has actually happened.
    bool m_IRExtendedMode = false;

    // Balance Board software tare/zero. m_BalanceRawKg holds the most
    // recent PRE-tare corner readings (factory-calibrated, offset not
    // applied) for TareBalanceBoard() to capture from; m_BalanceTareKg
    // holds the offset currently being subtracted.
    float m_BalanceRawKg[4]  = {0.f, 0.f, 0.f, 0.f}; // order: TR, BR, TL, BL
    float m_BalanceTareKg[4] = {0.f, 0.f, 0.f, 0.f};
    bool  m_BalanceHasRawReading = false;
};

} // namespace InputBridge::Wiimote
