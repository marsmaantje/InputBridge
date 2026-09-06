// src/Devices/Wiimote/WiimoteDevice.cpp
#include "WiimoteDevice.h"
#include "WiimoteDecoder.h"
#include "App/Log.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "WiimoteDevice";
constexpr int kReportBufSize = 22; // largest fixed report we consume (0x37 = 22 incl. report ID)
constexpr int kRegisterReadTimeoutMs = 250;

// WiiBrew "IR Camera#Initialization": delay >=50ms between every byte
// transmission to avoid landing in a random init state. Enforced via
// SDL_Delay() between every write in EnableIRCameraOnce() - must be an
// actual sleep, not incidental Bluetooth/HID scheduling latency.
constexpr Uint32 kIRInitStepDelayMs = 50;

// Settle time after ResetExtensionEncryption()'s writes before trusting a
// subsequent ID/data read. Needed in particular when that call is also
// what deactivates an active Motion Plus (WiiBrew: writing the standard
// extension-init bytes to 0xA400F0/FB "register-swaps" 0xA4xxxx back from
// the Motion Plus to whatever's on the regular/pass-through port) - reading
// too soon after that write can race the hardware's swap and return stale
// Motion Plus-shaped bytes instead of the real extension's ID, misreading
// a perfectly good Nunchuk/Classic Controller as Unknown.
constexpr Uint32 kExtensionSwapSettleMs = 50;

// How long to wait after SDL_hid_open_path() succeeds before running the
// handshake (Init(), including EnableIRCamera()) - see m_InitSettleAtMs's
// header comment for why a just-opened handle isn't necessarily a
// just-settled Bluetooth connection. 500ms comfortably covers the
// sub-second HID-channel negotiation windows observed, while still
// feeling responsive on a freshly-connected Wiimote.
constexpr Uint32 kConnectSettleMs = 500;

// Avoids relying on M_PI, which <cmath> doesn't define on MSVC without
// _USE_MATH_DEFINES (this project builds on Windows).
constexpr float kPi = 3.14159265358979323846f;
}

WiimoteDevice::WiimoteDevice(std::unique_ptr<IWiimoteTransport> transport, std::string hid_path, bool is_balance_board_hint)
    : m_Transport(std::move(transport)), m_Path(std::move(hid_path)) {
    m_Snapshot.hid_path = m_Path;
    m_Snapshot.is_balance_board = is_balance_board_hint;
    if (m_Transport) {
        // Non-blocking mode is set up inside the transport's own
        // constructor (see WiimoteHidTransport's ctor).
        //
        // Init() is deferred to Poll(), once the connection has had
        // kConnectSettleMs to settle - see m_InitSettleAtMs's header
        // comment for why running it synchronously here is unsafe.
        m_InitPending = true;
        m_InitSettleAtMs = SDL_GetTicks() + kConnectSettleMs;
    }
}

WiimoteDevice::~WiimoteDevice() {
    if (m_Transport) m_Transport->Close();
}

// -- Output report helpers -----------------------------------------------

namespace {
// Every output report's first payload byte carries the rumble bit in bit 0.
// `payload` should NOT include the leading report-ID byte - that's added
// here, matching IWiimoteTransport::Write's convention of
// report-ID-as-first-byte (itself inherited from SDL_hid_write's).
bool SendReport(IWiimoteTransport *transport, uint8_t report_id, bool rumble,
                 const uint8_t *payload, size_t payload_len) {
    if (!transport) return false;
    uint8_t buf[32] = {};
    buf[0] = report_id;
    if (payload && payload_len) std::memcpy(buf + 1, payload, std::min(payload_len, sizeof(buf) - 1));
    if (rumble) buf[1] |= 0x01;
    const size_t total = 1 + std::max<size_t>(payload_len, 1);
    return transport->Write(buf, total) >= 0;
}
} // namespace

bool WiimoteDevice::Init() {
    if (!m_Transport) return false;
    bool ok = true;

    // Ask for a status report so we learn battery + whether an extension is
    // already plugged in before we pick a data-reporting mode.
    {
        uint8_t p[1] = {0x00};
        ok &= SendReport(m_Transport.get(), OutReport::StatusRequest, m_RumbleBit, p, 1);
    }

    // Balance Boards have no IR/speaker hardware - skip straight to data
    // reporting. Real Wiimotes get the IR camera turned on.
    if (!m_Snapshot.is_balance_board) {
        ok &= EnableIRCamera();
    }

    // Steady-state data reporting mode. Real Wiimotes use buttons + accel +
    // 10 IR (basic) + 6 extension bytes - the richest mode that still
    // leaves room for Nunchuk/Classic/Guitar data. Balance Boards have no
    // IR/accel hardware worth reporting and need all 11 of their weight-
    // sensor bytes, which only fits report 0x34 (19 ext bytes, also carries
    // battery). Continuous bit (0x04) set so we get reports every tick even
    // when nothing changes - simpler polling loop.
    {
        uint8_t p[2] = {0x04, PreferredReportMode()};
        ok &= SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p, 2);
    }

    // Default to player LED 1 lit so the physical remote shows it's alive.
    SetPlayerLED(1);

    // Arm the bare-Motion-Plus probe (see m_MotionPlusNextProbeAtMs's
    // header comment for why this can't just ride on the extension-
    // changed path). Balance Boards have no Motion Plus port to probe.
    // Give it the same settle window as a regular extension gets before
    // Poll() acts on it, rather than probing with zero delay.
    if (!m_Snapshot.is_balance_board) {
        m_MotionPlusNextProbeAtMs = SDL_GetTicks() + 150;
    }

    return ok;
}

uint8_t WiimoteDevice::PreferredReportMode() const {
    if (m_Snapshot.is_balance_board) return InReport::CoreExt19;
    return m_IRExtendedMode ? InReport::CoreAccelIR12 : InReport::CoreAccelIR10Ext6;
}

bool WiimoteDevice::EnableIRCamera() {
    // WiiBrew's IR Camera#Initialization section documents this sequence as
    // inherently flaky: landing in one of 3 states (off/half-sensitivity/
    // full-sensitivity) is "pretty much random" even with the recommended
    // >=50ms inter-write delay, and its own fix is to repeat the whole
    // sequence rather than just adding delays. This does both: an enforced
    // inter-write delay (rather than relying on incidental Bluetooth/HID
    // scheduling latency) and a bounded retry with real verification
    // (status report bit 3) instead of trusting write-success alone.
    //
    // Runs synchronously inside Init(), once per newly-connected Wiimote -
    // not from the per-frame Poll() loop - so a worst case of all
    // kMaxAttempts failing costs a multi-second stall (up to ~1.8s with the
    // values below) but only on that one connection event. Judged
    // acceptable to turn "IR silently fails ~1/3 of the time" into
    // "reliably works, unlucky runs take a bit longer" - lower
    // kMaxAttempts for a shorter worst case if that tradeoff changes.
    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (EnableIRCameraOnce() && VerifyIRCameraEnabled()) {
            m_Snapshot.ir_enabled = true;
            // Clean slate for TickIRWatchdog(): m_LastIRReportMs == 0 means
            // "no IR report yet, don't flag as hijacked" (the status-report
            // check above doesn't count - it carries no IR data). Also
            // clear any stale hijack flag/attempt count from a prior cycle.
            m_LastIRReportMs = 0;
            m_Snapshot.ir_possibly_hijacked = false;
            m_IRReassertAttempts = 0;
            if (attempt > 0) {
                LOG_INFO(kTag, "IR camera enabled on attempt %d/%d for %s",
                         attempt + 1, kMaxAttempts, m_Path.c_str());
            }
            return true;
        }
        LOG_WARN(kTag, "IR camera enable attempt %d/%d failed verification for %s%s",
                 attempt + 1, kMaxAttempts, m_Path.c_str(),
                 (attempt + 1 < kMaxAttempts) ? " - retrying" : " - giving up");
    }
    // Exhausted retries - leave the hardware wherever it landed (still
    // probably "on" per WiiBrew's 3 possible outcomes, just maybe not
    // producing data) but don't claim success.
    m_Snapshot.ir_enabled = false;
    LOG_ERROR(kTag, "IR camera failed to enable after %d attempts for %s - "
                     "IR data will not be available this session",
              kMaxAttempts, m_Path.c_str());
    return false;
}

bool WiimoteDevice::EnableIRCameraOnce() {
    bool ok = true;
    uint8_t enable[1] = {0x04};
    ok &= SendReport(m_Transport.get(), OutReport::IRCameraEnable1, m_RumbleBit, enable, 1);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= SendReport(m_Transport.get(), OutReport::IRCameraEnable2, m_RumbleBit, enable, 1);
    SDL_Delay(kIRInitStepDelayMs);

    // toggle -> sensitivity block 1 -> block 2 -> mode -> toggle again,
    // each as a register write, each separated by the WiiBrew-recommended
    // >=50ms gap (kIRInitStepDelayMs). WriteRegister() itself is a single
    // blocking HID write (not a read-back), so the delay has to be enforced
    // here explicitly between calls rather than being any part of
    // WriteRegister()'s own timeout/wait logic.
    uint8_t toggle08 = 0x08;
    ok &= WriteRegister(Registers::IRModeToggle, &toggle08, 1);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRSensitivity1, kIRSensitivityWiiLevel3.block1.data(),
                         uint8_t(kIRSensitivityWiiLevel3.block1.size()));
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRSensitivity2, kIRSensitivityWiiLevel3.block2.data(),
                         uint8_t(kIRSensitivityWiiLevel3.block2.size()));
    SDL_Delay(kIRInitStepDelayMs);
    uint8_t mode = m_IRExtendedMode ? IRMode::Extended : IRMode::Basic;
    ok &= WriteRegister(Registers::IRMode, &mode, 1);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRModeToggle, &toggle08, 1);
    SDL_Delay(kIRInitStepDelayMs);

    return ok;
}

bool WiimoteDevice::VerifyIRCameraEnabled() {
    // Request a fresh status report and block briefly, mirroring
    // ReadRegister()'s spin-wait-with-deadline pattern. Status byte 3
    // ("LF") bit 3 (0x08) is WiiBrew's "IR camera enabled" bit - the
    // ground truth for whether the sequence above actually landed in a
    // working state, since per WiiBrew even a fully successful write
    // sequence can randomly land "on but not taking data".
    //
    // CRITICAL: Init() already sent its own fire-and-forget StatusRequest
    // before EnableIRCamera() ran, and never read the reply - it's still
    // sitting in the HID read queue, generated before the IR camera was
    // ever touched, so its IR bit is necessarily 0 regardless of this
    // attempt. Same for a stale reply left over from a previous failed
    // attempt in EnableIRCamera()'s retry loop. Bluetooth HID reports are
    // a FIFO with no way to match a reply to its request, so accepting
    // whichever status report arrives first can silently consume a stale
    // one and report a false "not enabled". Draining the queue immediately
    // before sending the request is safe here since nothing else writes to
    // this device concurrently (Init()/EnableIRCamera() run synchronously
    // on one thread) - anything already queued at this point predates the
    // request about to be sent.
    {
        uint8_t drain[kReportBufSize];
        while (m_Transport->Read(drain, sizeof(drain)) > 0) {
            // Discard, except keep buttons fresh from whatever's flushed,
            // same courtesy as the main wait loop below.
            if (drain[0] >= InReport::Core && drain[0] <= InReport::InterleavedB) {
                m_Snapshot.core = Decode::Buttons(drain + 1);
            }
        }
    }

    uint8_t p[1] = {0x00};
    if (!SendReport(m_Transport.get(), OutReport::StatusRequest, m_RumbleBit, p, 1)) return false;

    const Uint64 deadline = SDL_GetTicks() + kRegisterReadTimeoutMs;
    while (SDL_GetTicks() < deadline) {
        uint8_t buf[kReportBufSize] = {};
        const int n = m_Transport->Read(buf, sizeof(buf));
        if (n <= 0) {
            // Non-blocking transport, so an empty read returns immediately
            // rather than waiting - without a sleep here this busy-spins
            // SDL_hid_read() as fast as the CPU allows. Confirmed on
            // Linux/BlueZ: that spin can starve the Bluetooth stack's own
            // request/response servicing, delaying the very reply this
            // loop is waiting for. A short sleep costs negligible latency
            // but avoids pegging a core and competing with the transport.
            SDL_Delay(1);
            continue;
        }
        if (buf[0] == InReport::Status) {
            // (a1) 20 BB BB LF 00 00 VV - still route it through the normal
            // handler so battery/extension state stays current rather than
            // being silently consumed here.
            HandleStatusReport(buf);
            const bool ir_bit = (buf[3] & 0x08) != 0;
            LOG_VERBOSE(kTag, "IR verification status reply: LF=0x%02x -> IR bit %s",
                        buf[3], ir_bit ? "SET" : "clear");
            return ir_bit;
        }
        // Anything else arriving while we wait: at minimum keep buttons
        // fresh, matching ReadRegister()'s same fallback.
        if (buf[0] >= InReport::Core && buf[0] <= InReport::InterleavedB) {
            m_Snapshot.core = Decode::Buttons(buf + 1);
        }
    }
    LOG_WARN(kTag, "Timed out waiting for status reply during IR verification for %s", m_Path.c_str());
    return false; // timed out waiting for the status reply
}

bool WiimoteDevice::ResetExtensionEncryption() {
    // "New way" init (WiiBrew): write 0x55 -> 0xA400F0, 0x00 -> 0xA400FB,
    // 0x00 -> 0xA400F0. Works on all known official extensions and leaves
    // the ID + data bytes unencrypted, so try it first.
    //
    // Split out from InitExtension() so ActivateMotionPlus() can redo just
    // this reset before writing a passthrough-activation register, without
    // calling back into the rest of InitExtension() - which ends by
    // probing for Motion Plus again, so re-entering the whole function
    // there caused unbounded Init<->Activate recursion on any Wiimote with
    // a Nunchuk/Classic Controller behind an active Motion Plus.
    uint8_t v55 = 0x55, v00 = 0x00;
    bool ok = true;
    ok &= WriteRegister(Registers::ExtensionInitNew1, &v55, 1);
    ok &= WriteRegister(Registers::ExtensionInitNew2, &v00, 1);
    ok &= WriteRegister(Registers::ExtensionEncryption, &v00, 1);
    return ok;
}

bool WiimoteDevice::InitExtension() {
    if (!ResetExtensionEncryption()) return false;
    SDL_Delay(kExtensionSwapSettleMs);

    m_ExtensionEncrypted = false;

    ExtensionId6 id{};
    if (!ReadRegister(Registers::ExtensionId, 6, id.bytes.data())) {
        m_Snapshot.extension = ExtensionType::None;
    } else {
        ExtensionType classified = ClassifyExtension(id);
        if (classified == ExtensionType::Unknown) {
            // The "new way" write didn't produce a recognizable ID. Some
            // wireless/third-party Nunchuks ignore that write or ship with
            // encryption permanently on - fall back to the "old way" init
            // (write 0x00 -> 0xA40040 only, encryption stays ON) and
            // decrypt the ID bytes before classifying (WiimoteProtocol.h's
            // DecryptExtensionByte()).
            uint8_t v00b = 0x00;
            if (WriteRegister(Registers::ExtensionInitOld, &v00b, 1) &&
                ReadRegister(Registers::ExtensionId, 6, id.bytes.data())) {
                DecryptExtensionBytes(id.bytes.data(), id.bytes.size());
                const ExtensionType retry = ClassifyExtension(id);
                if (retry != ExtensionType::Unknown) {
                    classified = retry;
                    m_ExtensionEncrypted = true;
                    LOG_INFO(kTag, "Extension on %s only identified via the encrypted "
                                   "(\"old way\") init - treating its data as encrypted",
                                   m_Path.c_str());
                }
                // If the retry is still Unknown, leave `classified` as
                // whatever the "new way" read produced (Unknown/None) -
                // neither init variant produced something recognizable, so
                // there's nothing better to fall back to.
            }
        }
        m_Snapshot.extension = classified;
    }
    m_Snapshot.extension_encrypted = m_ExtensionEncrypted;

    // Explicitly set Data Format 0x01 at 0xA400FE for Classic Controllers
    if (m_Snapshot.extension == ExtensionType::ClassicController ||
        m_Snapshot.extension == ExtensionType::ClassicControllerPro) {
        uint8_t fmt = 0x01;
        WriteRegister(Registers::ExtensionDataFormat, &fmt, 1);
    }

    // A physical Balance Board self-identifies with type 0x0402 through
    // the regular extension port, so this branch alone is sufficient -
    // is_balance_board (the HID product-string hint) is unreliable over
    // Bluetooth (many stacks report a blank/generic string) and must never
    // gate this. Correct the hint here from the authoritative ID so every
    // other is_balance_board check downstream self-heals too.
    if (m_Snapshot.extension == ExtensionType::BalanceBoard) {
        const bool was_already_known = m_Snapshot.is_balance_board;
        m_Snapshot.is_balance_board = true;
        LoadBalanceBoardCalibration();

        // If enumeration missed this (common over Bluetooth), Init()
        // already ran assuming it wasn't a Balance Board - it left the IR
        // camera on and selected the buttons+accel+IR+6ext report mode
        // instead of the 19-ext-byte mode the 11 weight-sensor bytes need.
        // Redo the IR-dependent parts now that the flag is correct, and
        // re-send the report mode so real weight reports (0x34) start
        // arriving instead of the wrong ones (0x37).
        if (!was_already_known) {
            uint8_t irOff[1] = {0x00};
            SendReport(m_Transport.get(), OutReport::IRCameraEnable1, m_RumbleBit, irOff, 1);
            SendReport(m_Transport.get(), OutReport::IRCameraEnable2, m_RumbleBit, irOff, 1);
            m_Snapshot.ir_enabled = false;
            m_Snapshot.ir = {};

            uint8_t p[2] = {0x04, PreferredReportMode()};
            SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p, 2);
        }
    }

    // Probe for a Wii Motion Plus regardless of what's on the regular
    // extension port - it lives at its own register base (0xA60000) and
    // isn't mutually exclusive with a Nunchuk/Classic Controller (which it
    // can passthrough). Skip the probe on Balance Boards, which have no
    // MotionPlus port.
    if (!m_Snapshot.is_balance_board) {
        DetectMotionPlus();
    }

    return true;
}

bool WiimoteDevice::DetectMotionPlus() {
    ExtensionId6 id{};
    if (!ReadRegister(Registers::MotionPlusId, 6, id.bytes.data())) {
        m_MotionPlusPresent = false;
        m_MotionPlusActive = false;
        m_Snapshot.motion_plus = {};
        return false;
    }
    const ExtensionType classified = ClassifyExtension(id);
    if (classified != ExtensionType::MotionPlus) {
        m_MotionPlusPresent = false;
        m_MotionPlusActive = false;
        m_Snapshot.motion_plus = {};
        return false;
    }
    m_MotionPlusPresent = true;
    return ActivateMotionPlus();
}

bool WiimoteDevice::ActivateMotionPlus() {
    // Mode depends on whether the regular extension port already has
    // something plugged in: passthrough keeps that device's data flowing
    // (re-encoded by the MotionPlus) alongside the new gyro bytes; plain
    // activation is used when the port is empty. Per WiiBrew, standalone
    // activation is 0x04 written to 0xA600FE - not 0x55, which is the
    // unrelated "new way" extension-init byte at 0xA400F0 that actually
    // *deactivates* the MotionPlus.
    uint8_t mode = 0x04;
    uint32_t reg = Registers::MotionPlusInit;
    if (m_Snapshot.extension == ExtensionType::Nunchuk) {
        mode = 0x05;
        reg = Registers::MotionPlusInitNunchukPass;
    } else if (m_Snapshot.extension == ExtensionType::ClassicController ||
               m_Snapshot.extension == ExtensionType::ClassicControllerPro) {
        mode = 0x07;
        reg = Registers::MotionPlusInitClassicPass;
    }

    // Only reset the regular extension port (0xA400F0/FB) when we're about
    // to pass a Nunchuk/Classic Controller through - those registers belong
    // to the *regular* extension, not the Motion Plus. Sending that reset
    // in standalone mode (0x04), when nothing is plugged into that port,
    // stomps on the Motion Plus's own bus arbitration right before the
    // 0xA600FE activation write - detection still works fine (that's a
    // separate read at 0xA600FA), but the activation write silently fails,
    // so no gyro reports ever arrive despite m_MotionPlusPresent being true.
    if (mode != 0x04) {
        ResetExtensionEncryption();
    }

    const bool ok = WriteRegister(reg, &mode, 1);
    m_MotionPlusActive = ok;
    if (ok) {
        m_Snapshot.motion_plus.is_nunchuk_passthrough = (mode == 0x05);
        m_Snapshot.motion_plus.is_classic_passthrough = (mode == 0x07);

        // WiiBrew notes standalone mode auto-reports status (triggering a Data
        // Reporting Mode resend) only without a pass-through extension attached.
        // Real hardware and clones unreliably omit this auto-report,
        // which halts data reporting and freezes gyro input.
        // Explicitly resend the mode here to ensure report flow.
        uint8_t p[2] = {0x04, PreferredReportMode()};
        SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p, 2);
    }
    return ok;
}

bool WiimoteDevice::LoadBalanceBoardCalibration() {
    // A plain single read works on some boards, but WiiBrew's captured Wii
    // init trace shows the real Wii performs a "wake" sequence first -
    // several writes of 0xAA to register 0xf1, interleaved with reads of
    // the calibration block - before trusting the 4 weight sensors.
    // Skipping this is a documented cause of one or more sensors reading
    // back a constant raw value (~0kg after calibration) until the next
    // power/connect cycle. Meaning of the 0xf1 writes isn't documented
    // (WiiBrew speculates calibration-related) - this reproduces the
    // trace's shape rather than explaining it.
    uint8_t aa1[1] = {0xAA};
    WriteRegister(Registers::BalanceBoardWake, aa1, 1);
    WriteRegister(Registers::BalanceBoardWake, aa1, 1);
    WriteRegister(Registers::BalanceBoardWake, aa1, 1);

    // One throwaway read of the calibration block's first half, matching
    // the trace's interleaved reads - some boards need a register access
    // between the initial writes and the final 7-byte burst below to start
    // responding on all 4 sensors.
    uint8_t discard[16] = {};
    ReadRegister(Registers::ExtensionCalib, 16, discard);

    // The trace's "Write f1: aa aa aa 55 aa aa aa" burst - a single write
    // spanning 7 bytes, not 7 separate single-byte writes (single-byte
    // writes of just 0xAA were already sent above; this is the distinct
    // longer write that follows in the captured sequence).
    uint8_t burst[7] = {0xAA, 0xAA, 0xAA, 0x55, 0xAA, 0xAA, 0xAA};
    WriteRegister(Registers::BalanceBoardWake, burst, 7);

    WriteRegister(Registers::BalanceBoardWake, aa1, 1);
    WriteRegister(Registers::BalanceBoardWake, aa1, 1);

    // The trace waits here before the sensors settle; 50ms is a
    // conservative margin over what's needed in practice without adding
    // noticeable connect-time latency.
    SDL_Delay(50);

    WriteRegister(Registers::BalanceBoardWake, aa1, 1);

    uint8_t block[32] = {};
    if (!ReadRegister(Registers::ExtensionCalib, 32, block)) return false;

    // Reference Temperature + the byte after it (0xA40060/61) - not part
    // of the 32-byte block, but needed to reproduce the CRC32 (see
    // ParseBalanceBoardCalibration()). If this read fails, fall through
    // with zeroed bytes - the CRC just won't match, correctly treating
    // this as a bad read rather than a corrupted calibration block.
    uint8_t ref_temp[2] = {};
    ReadRegister(Registers::ExtensionCalibRefTemp, 2, ref_temp);

    BalanceBoardCalibration parsed = Decode::ParseBalanceBoardCalibration(block, ref_temp);
    if (!parsed.valid) {
        // CRC32 mismatch: the read was corrupted. Keep any existing
        // calibration rather than clobbering it with bad/zeroed data.
        return false;
    }
    m_BalanceCal = parsed;
    return true;
}

// -- Feedback -------------------------------------------------------------

void WiimoteDevice::SetPlayerLED(int player_1to4) {
    const uint8_t bit = uint8_t(0x10 << std::clamp(player_1to4 - 1, 0, 3));
    SetLEDMask(bit);
}

void WiimoteDevice::SetLEDMask(uint8_t mask4bits) {
    uint8_t p[1] = {mask4bits};
    SendReport(m_Transport.get(), OutReport::LEDs, m_RumbleBit, p, 1);
    m_Snapshot.led_mask = mask4bits;
}

void WiimoteDevice::SetRumble(float intensity) {
    m_RumbleIntensity = std::clamp(intensity, 0.0f, 1.0f);
    m_Snapshot.rumble_intensity = m_RumbleIntensity;
    // Restart the PWM period so a fresh call always begins at phase 0
    // (motor on, for nonzero intensity) rather than wherever the previous
    // target's cycle was - otherwise a call landing late in a period could
    // read as "off" for up to kRumblePwmPeriodMs despite a nonzero
    // intensity. Also drop any duty-cycle debt from the previous target.
    m_RumbleCycleStartMs = SDL_GetTicks();
    m_RumbleDutyDebtMs = 0.0f;
    UpdateRumblePWM(); // apply immediately rather than waiting for the next Poll()
}

void WiimoteDevice::UpdateRumblePWM() {
    const Uint64 now = SDL_GetTicks();
    bool desired_bit;

    if (m_RumbleIntensity <= 0.0f) {
        desired_bit = false;
        m_RumbleDutyDebtMs = 0.0f;
    } else if (m_RumbleIntensity >= 1.0f) {
        desired_bit = true;
        m_RumbleDutyDebtMs = 0.0f;
    } else {
        // Time since we last got a chance to check/toggle the bit. Under
        // normal conditions (Poll() faster than the carrier period) this
        // is a few ms and everything below is a no-op. A frame hitch, lost
        // focus, or a Bluetooth stall all show up as one bigger gap; if it
        // spans one or more whole carrier periods, the line sat wherever
        // it last was for those periods rather than tracking `intensity`.
        // Credit/debit the resulting shortfall or excess into
        // m_RumbleDutyDebtMs and pay it back via the current period's
        // on/off boundary, instead of silently accepting the lost accuracy.
        const Uint64 gapMs = (m_RumbleLastPollMs == 0) ? 0 : (now - m_RumbleLastPollMs);
        const Uint64 skippedPeriods = gapMs / kRumblePwmPeriodMs;
        if (skippedPeriods > 0) {
            const float targetOnPerSkippedMs    = m_RumbleIntensity * float(kRumblePwmPeriodMs);
            const float deliveredOnPerSkippedMs = m_RumbleBit ? float(kRumblePwmPeriodMs) : 0.0f;
            m_RumbleDutyDebtMs += float(skippedPeriods) * (targetOnPerSkippedMs - deliveredOnPerSkippedMs);

            // Bound the debt so a long stall can't demand an absurdly long
            // unbroken on/off burst once polling resumes - cap at a few
            // periods' worth and let any remainder be lost, as it would
            // have been without this mechanism.
            const float kMaxDebtMs = float(kRumblePwmPeriodMs) * 4.0f;
            m_RumbleDutyDebtMs = std::clamp(m_RumbleDutyDebtMs, -kMaxDebtMs, kMaxDebtMs);
        }

        const Uint64 phase = (now - m_RumbleCycleStartMs) % kRumblePwmPeriodMs;

        // Nudge this period's on-duration by whatever debt is outstanding
        // (positive = owe more on-time, negative = delivered too much),
        // clamped to a single period's own bounds so correction always
        // spreads across 1+ periods rather than landing as one instant
        // jump to fully on/off. Whatever fraction of the debt this period
        // actually got to absorb is no longer owed.
        const float baseOnMs = m_RumbleIntensity * float(kRumblePwmPeriodMs);
        const float correctedOnMs = std::clamp(baseOnMs + m_RumbleDutyDebtMs,
                                                0.0f, float(kRumblePwmPeriodMs));
        m_RumbleDutyDebtMs -= (correctedOnMs - baseOnMs);

        desired_bit = phase < Uint64(correctedOnMs);
    }
    m_RumbleLastPollMs = now;

    if (desired_bit == m_RumbleBit) return; // no edge to act on - skip the HID write

    m_RumbleBit = desired_bit;
    m_Snapshot.rumble_on = desired_bit;
    // Any report re-asserts the rumble bit; a dedicated Rumble (0x10) report
    // with an otherwise-empty payload is the lightest way to do that on demand.
    uint8_t p[1] = {0x00};
    SendReport(m_Transport.get(), OutReport::Rumble, m_RumbleBit, p, 1);
}

// -- Speaker ---------------------------------------------------------------

bool WiimoteDevice::EnableSpeaker(uint32_t sample_rate_hz, uint8_t volume, SpeakerAudioFormat format) {
    if (!m_Transport) return false;
    if (sample_rate_hz == 0) sample_rate_hz = 2000;

    // ADPCM4's hardware volume register only goes to 0x40 (WiiBrew's
    // Speaker Configuration section) - clamp rather than writing an
    // out-of-range value whose hardware behavior isn't documented.
    if (format == SpeakerAudioFormat::ADPCM4 && volume > 0x40) volume = 0x40;

    // A live format switch leaves anything queued in the old format
    // meaningless (PCM8 bytes played back as ADPCM4 nibbles is just
    // noise) - see QueueADPCM4()'s comment; this discards the queue and
    // resets the ADPCM encoder exactly like StopSpeaker() does.
    if (m_SpeakerFormat != format) StopSpeaker();

    // WiiBrew "Wiimote#Speaker / Initialization Sequence", steps 1-7:
    //   1. Enable speaker      (0x04 -> Report 0x14)
    //   2. Mute speaker        (0x04 -> Report 0x19) - reconfiguring a live
    //      speaker is what produces the garbled-squawk-on-connect several
    //      other implementations report; muting first avoids it.
    //   3. Write 0x01 -> 0xa20009
    //   4. Write 0x08 -> 0xa20001 (yes, before step 5 overwrites the same
    //      address as part of the 7-byte block - this is WiiBrew's literal
    //      documented sequence, kept as-is for parity with known-working
    //      implementations rather than "optimized" away)
    //   5. Write the 7-byte format/rate/volume block -> 0xa20001-0xa20007
    //   6. Write 0x01 -> 0xa20008
    //   7. Unmute speaker      (0x00 -> Report 0x19)
    bool ok = true;

    uint8_t enable = 0x04;
    ok &= SendReport(m_Transport.get(), OutReport::SpeakerEnable, m_RumbleBit, &enable, 1);

    uint8_t mute = 0x04;
    ok &= SendReport(m_Transport.get(), OutReport::SpeakerMute, m_RumbleBit, &mute, 1);

    uint8_t v01 = 0x01;
    ok &= WriteRegister(Registers::SpeakerInitFlag, &v01, 1);

    uint8_t v08 = 0x08;
    ok &= WriteRegister(Registers::SpeakerConfig, &v08, 1);

    // rate register value = clock / desired Hz (WiiBrew's formula; integer
    // division, so the achieved rate may differ slightly). Each format has
    // its own clock (kSpeakerPcmClockHz / kSpeakerAdpcmClockHz,
    // WiimoteProtocol.h) - using the wrong one configures a rate 2x off.
    const bool is_adpcm = format == SpeakerAudioFormat::ADPCM4;
    const uint32_t clock_hz = is_adpcm ? kSpeakerAdpcmClockHz : kSpeakerPcmClockHz;
    const uint32_t rate_value = clock_hz / sample_rate_hz;
    const uint8_t config[7] = {
        0x00,                                // unknown, always 0x00 per WiiBrew
        is_adpcm ? SpeakerFormat::Adpcm4 : SpeakerFormat::Pcm8,
        uint8_t(rate_value & 0xFF),           // sample rate, little-endian
        uint8_t((rate_value >> 8) & 0xFF),
        volume,                               // 0x00-0xFF (PCM8) / 0x00-0x40 (ADPCM4), already clamped above
        0x00, 0x00,                           // unknown, always 0x00 per WiiBrew
    };
    ok &= WriteRegister(Registers::SpeakerConfig, config, sizeof(config));

    uint8_t v01b = 0x01;
    ok &= WriteRegister(Registers::SpeakerCommitFlag, &v01b, 1);

    uint8_t unmute = 0x00;
    ok &= SendReport(m_Transport.get(), OutReport::SpeakerMute, m_RumbleBit, &unmute, 1);

    m_SpeakerEnabled = ok;
    m_SpeakerSampleRateHz = sample_rate_hz;
    m_SpeakerVolume = volume;
    m_SpeakerFormat = format;
    // A (re)configure is exactly the kind of state discontinuity
    // QueueADPCM4()'s comment describes - start its encoder clean so it
    // doesn't carry predictor/step state across what's effectively a new
    // stream as far as the hardware is concerned. Harmless no-op in PCM8
    // mode (nothing reads m_AdpcmEncoder there).
    m_AdpcmEncoder.Reset();

    // Pace TickSpeaker() so a kSpeakerMaxChunkBytes chunk drains roughly
    // as fast as the Wiimote consumes it - not faster (piles up in its
    // buffer) or slower (starves it, causing dropouts/pops). ADPCM4 packs
    // 2 samples/byte vs. PCM8's 1, so the same chunk covers twice the
    // playback time in ADPCM4 - using PCM8 math here would pace
    // transmission at half the rate ADPCM4 actually needs.
    const uint32_t samples_per_chunk = uint32_t(kSpeakerMaxChunkBytes) * (is_adpcm ? 2 : 1);
    m_SpeakerChunkIntervalMs = std::max<Uint32>(
        1, Uint32((1000ull * samples_per_chunk) / sample_rate_hz));
    m_SpeakerNextChunkAtMs = SDL_GetTicks();

    if (!ok) {
        LOG_WARN(kTag, "EnableSpeaker() had at least one failed HID write for %s "
                 "(device unplugged mid-sequence?)", m_Path.c_str());
    }
    return ok;
}

void WiimoteDevice::DisableSpeaker() {
    if (m_Transport) {
        uint8_t off = 0x00;
        SendReport(m_Transport.get(), OutReport::SpeakerEnable, m_RumbleBit, &off, 1);
    }
    m_SpeakerEnabled = false;
    m_SpeakerSampleRateHz = 0;
    m_SpeakerVolume = 0;
    StopSpeaker();
}

void WiimoteDevice::QueuePCM8(const int8_t *samples, size_t count) {
    if (!samples || !count) return;
    m_SpeakerQueue.insert(m_SpeakerQueue.end(), samples, samples + count);
}

void WiimoteDevice::QueueADPCM4(const int16_t *samples, size_t count) {
    if (!samples || !count) return;
    std::vector<uint8_t> packed;
    m_AdpcmEncoder.Encode(samples, count, packed);
    if (packed.empty()) return;
    // Raw bytes, not sample values - a byte-for-byte copy regardless of
    // int8_t's signedness is exactly what's wanted here (TickSpeaker()
    // memcpy()s these straight into the HID report), so go through
    // memcpy rather than an implicit/narrowing per-element conversion.
    const size_t old_size = m_SpeakerQueue.size();
    m_SpeakerQueue.resize(old_size + packed.size());
    std::memcpy(m_SpeakerQueue.data() + old_size, packed.data(), packed.size());
}

namespace {
// Shared amplitude envelope for PlayBeep()'s two format-specific sample
// loops below: a plain sine tone with a short linear fade-in/out (~5ms or
// 10% of the tone, whichever is shorter) to avoid the audible click a
// hard-edged buffer start/stop produces on this speaker. Returns -1..1;
// callers scale to their own format's headroom-adjusted full scale.
float BeepEnvelope(size_t i, size_t sample_count, size_t fade_samples,
                    float freq_hz, uint32_t sample_rate_hz) {
    const float t = float(i) / float(sample_rate_hz);
    float amplitude = 1.0f;
    if (fade_samples > 0) {
        if (i < fade_samples) amplitude = float(i) / float(fade_samples);
        else if (i >= sample_count - fade_samples) amplitude = float(sample_count - 1 - i) / float(fade_samples);
    }
    return amplitude * std::sin(2.0f * kPi * freq_hz * t);
}
} // namespace

void WiimoteDevice::PlayBeep(float freq_hz, uint32_t duration_ms, uint32_t sample_rate_hz,
                              uint8_t volume, SpeakerAudioFormat format) {
    // WiiBrew's suggested rate differs per format (2000Hz PCM8 to keep the
    // Bluetooth link fed at that format's higher per-sample cost; 3000Hz -
    // its "standard value" - for ADPCM4). Picking the wrong one for the
    // format is what makes a "beep" sound like an aliased buzz.
    if (sample_rate_hz == 0) {
        sample_rate_hz = (format == SpeakerAudioFormat::ADPCM4) ? 3000 : 2000;
    }

    // Volume 0 means "play nothing" (silence is enforced by never sending
    // data, not the hardware register - see TickSpeaker()). No point
    // running the enable sequence or generating samples that would just
    // be discarded.
    if (volume == 0) {
        StopSpeaker();
        return;
    }
    if (format == SpeakerAudioFormat::ADPCM4 && volume > 0x40) volume = 0x40; // see EnableSpeaker()

    // Only re-run the (synchronous, multi-write) enable sequence if
    // something it actually controls changed - rate, volume, or format -
    // so repeated same-settings beeps (e.g. a UI click sound) can queue
    // back-to-back without redoing the register dance and its mute/unmute
    // click each time. Checking rate/volume alone was a past bug: a
    // repeat call with a new `volume` but the same rate silently skipped
    // EnableSpeaker() and kept the first call's volume; format needs the
    // same check or switching formats between beeps would silently keep
    // encoding in the old one.
    if (!m_SpeakerEnabled || m_SpeakerSampleRateHz != sample_rate_hz ||
        m_SpeakerVolume != volume || m_SpeakerFormat != format) {
        if (!EnableSpeaker(sample_rate_hz, volume, format)) return;
    }

    const size_t sample_count = size_t((uint64_t(sample_rate_hz) * duration_ms) / 1000);
    const size_t fade_samples = std::min(sample_count / 10, size_t(sample_rate_hz) * 5 / 1000);

    if (format == SpeakerAudioFormat::ADPCM4) {
        // Peak amplitude held to ~80% of int16 full-scale - ADPCM's own
        // quantization error means driving the source signal to true
        // full-scale is more likely to clip on peaks than linear PCM
        // would be, on top of the headroom EnableSpeaker()'s comment
        // already describes wanting below the volume register's own gain.
        std::vector<int16_t> samples(sample_count);
        for (size_t i = 0; i < sample_count; ++i) {
            const float s = BeepEnvelope(i, sample_count, fade_samples, freq_hz, sample_rate_hz);
            samples[i] = int16_t(std::clamp(s * 26214.0f, -26214.0f, 26214.0f));
        }
        QueueADPCM4(samples.data(), samples.size());
    } else {
        // Peak amplitude held below full-scale (100 of a possible 127) -
        // same headroom rationale as above.
        std::vector<int8_t> samples(sample_count);
        for (size_t i = 0; i < sample_count; ++i) {
            const float s = BeepEnvelope(i, sample_count, fade_samples, freq_hz, sample_rate_hz);
            samples[i] = int8_t(std::clamp(s * 100.0f, -100.0f, 100.0f));
        }
        QueuePCM8(samples.data(), samples.size());
    }
}

void WiimoteDevice::StopSpeaker() {
    m_SpeakerQueue.clear();
    m_SpeakerQueuePos = 0;
    // See QueueADPCM4()'s comment: once queued-but-unsent bytes are
    // discarded, the host's encoder state no longer corresponds to
    // anything the hardware decoder actually received, so it has to reset
    // too rather than silently drifting further out of sync on the next
    // QueueADPCM4() call. Harmless no-op in PCM8 mode.
    m_AdpcmEncoder.Reset();
}

void WiimoteDevice::TickSpeaker() {
    if (!m_SpeakerEnabled || !m_Transport) return;

    // Treat volume 0 as "play nothing" rather than trusting the hardware
    // gain register to produce true silence at its documented minimum -
    // WiiBrew notes "the full purpose of these bytes is not known", and
    // real hardware has been confirmed to still output sound at VV=0x00.
    // Anything still queued when volume drops to 0 mid-playback is
    // discarded rather than silently sent anyway.
    if (m_SpeakerVolume == 0) {
        if (!m_SpeakerQueue.empty()) StopSpeaker();
        return;
    }

    if (m_SpeakerQueuePos >= m_SpeakerQueue.size()) {
        // Fully drained - reset to an empty queue rather than letting
        // m_SpeakerQueuePos grow unbounded across many small QueuePCM8()
        // calls over a long session.
        if (!m_SpeakerQueue.empty()) StopSpeaker();
        return;
    }

    const Uint64 now = SDL_GetTicks();

    // Send every chunk whose scheduled time has passed, not just one.
    // Poll() runs at the host's frame rate (commonly ~16.67ms at 60fps),
    // which can be slower than the ~10ms cadence 2000Hz 8-bit PCM needs -
    // sending only one chunk per Poll() would under-deliver, starving the
    // speaker's buffer. That starvation is what crackle/stutter sounds
    // like on this hardware, not a bad waveform. Bounded
    // (kMaxChunksPerTick) so a real stall can't dump an unbounded backlog
    // into one burst of HID writes.
    constexpr int kMaxChunksPerTick = 8;
    int sent = 0;
    while (m_SpeakerQueuePos < m_SpeakerQueue.size() &&
           now >= m_SpeakerNextChunkAtMs &&
           sent < kMaxChunksPerTick) {
        const size_t remaining = m_SpeakerQueue.size() - m_SpeakerQueuePos;
        const uint8_t n = uint8_t(std::min<size_t>(remaining, kSpeakerMaxChunkBytes));

        // Report 0x18 payload is always the full LL byte + 20 data bytes,
        // even for a short final chunk - SendReport()'s zero-initialized
        // buf[32] already leaves any bytes past `n` as padding zeroes.
        uint8_t p[1 + kSpeakerMaxChunkBytes] = {};
        p[0] = uint8_t(n << 3); // LL: length, shifted left 3 bits (WiiBrew)
        std::memcpy(p + 1, m_SpeakerQueue.data() + m_SpeakerQueuePos, n);
        SendReport(m_Transport.get(), OutReport::SpeakerData, m_RumbleBit, p, sizeof(p));

        m_SpeakerQueuePos += n;
        // Schedule from where the PREVIOUS chunk was due, not from `now` -
        // advancing from `now` each time would let delivery drift below
        // the target rate under sustained Poll() jitter, reintroducing
        // the starvation this loop exists to fix.
        m_SpeakerNextChunkAtMs += m_SpeakerChunkIntervalMs;
        ++sent;
    }

    // If still behind after kMaxChunksPerTick catch-up sends, resync to
    // now rather than leaving the schedule arbitrarily far in the past -
    // otherwise every future tick would think it's perpetually catching
    // up and burst-send indefinitely.
    if (now >= m_SpeakerNextChunkAtMs)
        m_SpeakerNextChunkAtMs = now + m_SpeakerChunkIntervalMs;
}

// -- Register read/write (synchronous, bounded wait) ---------------------

bool WiimoteDevice::WriteRegister(uint32_t address, const uint8_t *data, uint8_t size) {
    if (!m_Transport || size > 16) return false;
    uint8_t p[21] = {};
    p[0] = kRegisterFlag; // select control-register space, not EEPROM
    p[1] = uint8_t((address >> 16) & 0xFF);
    p[2] = uint8_t((address >> 8) & 0xFF);
    p[3] = uint8_t(address & 0xFF);
    p[4] = size;
    if (data && size) std::memcpy(p + 5, data, size);
    return SendReport(m_Transport.get(), OutReport::WriteMemory, m_RumbleBit, p, sizeof(p));
}

bool WiimoteDevice::ReadRegister(uint32_t address, uint16_t size, uint8_t *out) {
    if (!m_Transport || !out) return false;

    uint16_t remaining = size;
    uint32_t addr = address;
    uint8_t *dst = out;

    while (remaining > 0) {
        const uint16_t chunk = std::min<uint16_t>(remaining, 16);

        uint8_t p[6];
        p[0] = kRegisterFlag;
        p[1] = uint8_t((addr >> 16) & 0xFF);
        p[2] = uint8_t((addr >> 8) & 0xFF);
        p[3] = uint8_t(addr & 0xFF);
        p[4] = uint8_t((chunk >> 8) & 0xFF);
        p[5] = uint8_t(chunk & 0xFF);
        if (!SendReport(m_Transport.get(), OutReport::ReadMemory, m_RumbleBit, p, sizeof(p)))
            return false;

        // Poll for the 0x21 reply. Regular data reports arriving while we
        // wait go through the normal HandleReport() decoder (safe here -
        // it never calls back into ReadRegister()) so accel/IR/extension/
        // motion_plus stay live for the whole read instead of freezing.
        const Uint64 deadline = SDL_GetTicks() + kRegisterReadTimeoutMs;
        bool got = false;
        while (SDL_GetTicks() < deadline) {
            uint8_t buf[kReportBufSize] = {};
            const int n = m_Transport->Read(buf, sizeof(buf));
            if (n <= 0) {
                // Same busy-spin hazard as VerifyIRCameraEnabled()'s wait
                // loop - non-blocking transport means an empty read
                // returns instantly, so without this sleep the loop would
                // spin at full CPU and starve the Bluetooth stack of the
                // time it needs to deliver the reply.
                SDL_Delay(1);
                continue;
            }
            if (buf[0] == InReport::ReadMemoryData) {
                // (a1) 21 BB BB SE FF FF DD..DD
                const uint8_t se = buf[3];
                const uint8_t err = se & 0x0F;
                const uint8_t got_size = (se >> 4) + 1;
                if (err != 0) return false; // read from nonexistent/write-only register
                const uint16_t got_addr_lo = (uint16_t(buf[4]) << 8) | buf[5];
                (void)got_addr_lo; // available for stricter validation if desired
                const uint16_t n_copy = std::min<uint16_t>(got_size, chunk);
                std::memcpy(dst, buf + 6, n_copy);
                got = true;
                break;
            }
            // Any other report while waiting: decode it exactly like Poll()
            // would, so nothing goes stale just because a register read is
            // in flight.
            HandleReport(buf, n);
        }
        if (!got) return false;

        dst += chunk;
        addr += chunk;
        remaining -= chunk;
    }
    return true;
}

// -- Input report handling -----------------------------------------------

void WiimoteDevice::Poll() {
    if (!m_Transport) return;

    // Keep the rumble PWM's on/off line current every tick, independent of
    // whether any input reports arrived this frame - it has its own timing
    // (kRumblePwmPeriodMs) unrelated to the Wiimote's own report cadence.
    UpdateRumblePWM();
    // Same "own timing, unrelated to report cadence" rationale as
    // UpdateRumblePWM() above - see TickSpeaker()'s declaration comment.
    TickSpeaker();

    uint8_t buf[kReportBufSize];
    for (;;) {
        const int n = m_Transport->Read(buf, sizeof(buf));
        if (n <= 0) break; // no more pending reports (non-blocking handle)
        HandleReport(buf, n);
    }

    // Run the deferred handshake once the connection has had a moment to
    // settle - see m_InitSettleAtMs's header comment. Checked before the
    // extension-settle handling below since InitExtension() being
    // triggered off Init()'s own StatusRequest still works the same way
    // once Init() actually runs.
    if (m_InitPending && SDL_GetTicks() >= m_InitSettleAtMs) {
        m_InitPending = false;
        Init();
    }

    // If we're waiting for an extension to settle after a connect event,
    // check whether enough time has passed to (re)try identification.
    if (m_ExtensionPendingInit && SDL_GetTicks() >= m_ExtensionSettleAtMs) {
        m_ExtensionPendingInit = false;
        InitExtension();
    }

    // Independently keep probing for a bare Motion Plus - see
    // m_MotionPlusNextProbeAtMs's header comment for why this can't just
    // ride on the extension-changed path above. Guarded on m_InitPending
    // being false (not just the deadline) so this can't fire on the very
    // first few Poll() ticks, before Init() has even run once and set a
    // real deadline. Stops re-arming (and re-reading the register) once
    // a Motion Plus is actually found.
    if (!m_InitPending && !m_MotionPlusPresent && !m_Snapshot.is_balance_board &&
        SDL_GetTicks() >= m_MotionPlusNextProbeAtMs) {
        m_MotionPlusNextProbeAtMs = SDL_GetTicks() + 8000; // WiiBrew's own suggested re-poll interval
        DetectMotionPlus();
    }

    // Keep retrying extension identification while something is physically
    // plugged in but the last attempt(s) came back unclassified - see
    // m_ExtensionRetryAtMs's header comment for why a single settle-timed
    // attempt isn't always enough (e.g. a slow-powering Nunchuk).
    //
    // Gated on !m_MotionPlusPresent: once a Motion Plus is active it owns
    // the regular extension address space (0xA4xxxx gets "register-swapped"
    // to the Motion Plus itself - WiiBrew's Wii Motion Plus page), so a
    // read there legitimately classifies as Unknown/None and is NOT a
    // failed identification needing a retry. Retrying anyway would call
    // InitExtension(), whose ResetExtensionEncryption() sends the standard
    // extension-init bytes to 0xA400F0/FB - exactly the sequence WiiBrew
    // documents as deactivating an active Motion Plus via that same
    // register swap. Without this guard, an active standalone Motion Plus
    // gets silently deactivated ~1s after every (re)activation, and one
    // behind a Nunchuk/Classic Controller flip-flops between passthrough
    // device and Motion Plus as each retry knocks it back down and
    // DetectMotionPlus()'s own re-probe (at the end of InitExtension())
    // races to bring it back up.
    if (!m_InitPending && !m_ExtensionPendingInit && !m_MotionPlusPresent &&
        m_ExtensionPortConnected && !m_Snapshot.is_balance_board &&
        (m_Snapshot.extension == ExtensionType::Unknown || m_Snapshot.extension == ExtensionType::None) &&
        SDL_GetTicks() >= m_ExtensionRetryAtMs) {
        m_ExtensionRetryAtMs = SDL_GetTicks() + 1000;
        InitExtension();
    }

    // Detect and correct for a second process (typically Steam Input - see
    // TickIRWatchdog()'s comment) silently changing our data reporting
    // mode after the fact.
    TickIRWatchdog();
}

void WiimoteDevice::HandleReport(const uint8_t *buf, int len) {
    m_Snapshot.connected = true;
    m_Snapshot.last_report_ms = SDL_GetTicks();
    switch (buf[0]) {
        case InReport::Status:
            HandleStatusReport(buf);
            break;
        case InReport::CoreAccelIR10Ext6:
            if (len >= 22) DecodeCoreAccelIR10Ext6(buf);
            break;
        case InReport::CoreAccelIR12:
            if (len >= 18) DecodeCoreAccelIR12(buf);
            break;
        case InReport::CoreExt19:
            if (len >= 22) DecodeCoreExt19(buf);
            break;
        case InReport::ReadMemoryData:
        case InReport::Acknowledge:
            // Consumed synchronously inside ReadRegister()/write acks; a
            // stray one here (e.g. a write ack while not reading) is safe
            // to ignore.
            break;
        default:
            // Any report we haven't special-cased still starts with the
            // core button bytes (except 0x3d) - keep buttons fresh anyway.
            if (buf[0] != InReport::Ext21 && len >= 3) {
                m_Snapshot.core = Decode::Buttons(buf + 1);
            }
            break;
    }
}

void WiimoteDevice::HandleStatusReport(const uint8_t *buf) {
    // (a1) 20 BB BB LF 00 00 VV
    m_Snapshot.core = Decode::Buttons(buf + 1);
    const uint8_t lf = buf[3];
    const uint8_t battery = buf[6];
    m_Snapshot.battery = ClassifyWiimoteBattery(battery);

    const bool ext_connected = lf & 0x02;
    const bool changed = (ext_connected != m_ExtensionPortConnected);
    m_ExtensionPortConnected = ext_connected;
    // Once a Motion Plus is present, this bit is documented-flaky and no
    // longer drives re-detection - see m_MotionPlusExtConnected's comment.
    // DecodeCoreAccelIR10Ext6() watches the Motion Plus's own
    // extension_connected bit instead for that case.
    if (changed && !m_MotionPlusPresent) {
        HandleExtensionChanged();
    }

    // Per WiiBrew: after ANY status report (requested or unsolicited), the
    // reporting mode must be re-sent or no further data reports will arrive.
    uint8_t p[2] = {0x04, PreferredReportMode()};
    SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p, 2);
}

void WiimoteDevice::HandleExtensionChanged() {
    // Give the extension ~150ms to power up / settle before we try to read
    // its ID - reading too early is a common source of misdetection.
    m_ExtensionPendingInit = true;
    m_ExtensionSettleAtMs = SDL_GetTicks() + 150;
    // First retry (if the settle-timed attempt above still comes back
    // Unknown/None) follows a bit after that attempt, not immediately.
    m_ExtensionRetryAtMs = m_ExtensionSettleAtMs + 1000;
    m_Snapshot.extension = ExtensionType::None;
    m_Snapshot.nunchuk = {};
    m_Snapshot.classic = {};
    m_Snapshot.guitar = {};
    m_Snapshot.balance_board = {};
    m_Snapshot.motion_plus = {};
    m_BalanceCal.reset();
    m_MotionPlusPresent = false;
    m_MotionPlusActive = false;
    m_MotionPlusExtConnected = -1; // baseline unknown again until the next MP report
    // Let Poll()'s bare-Motion-Plus probe (see m_MotionPlusNextProbeAtMs's
    // header comment) run again on its next tick instead of possibly
    // waiting out whatever was left of the previous ~8s window - relevant
    // e.g. right after a Nunchuk is unplugged from behind a Motion Plus.
    m_MotionPlusNextProbeAtMs = 0;
}

void WiimoteDevice::DecodeCoreAccelIR10Ext6(const uint8_t *buf) {
    // (a1) 37 BB BB AA AA AA II II II II II II II II II II EE EE EE EE EE EE
    //       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21
    const uint8_t *bb = buf + 1;
    const uint8_t *aa = buf + 3;
    const uint8_t *ir = buf + 6;
    const uint8_t *ee = buf + 16;

    m_Snapshot.core  = Decode::Buttons(bb);
    m_Snapshot.accel = Decode::Accel(bb, aa);
    m_Snapshot.ir    = Decode::IRBasic(ir);
    m_LastIRReportMs = SDL_GetTicks(); // fed to TickIRWatchdog() - see its comment
    m_Snapshot.ir_possibly_hijacked = false; // this report proves our mode is still in effect right now

    // Once active, the MotionPlus takes over the extension byte slot: its
    // own gyro data is distinguished from a regular extension's data by
    // ee[5] bit 1 == 1 (the "extension identifier" bit WiiBrew documents
    // for the DE data format).
    if (m_MotionPlusActive && (ee[5] & 0x02)) {
        m_Snapshot.motion_plus = Decode::MotionPlus(ee, 6);
        m_Snapshot.motion_plus.is_nunchuk_passthrough =
            (m_Snapshot.extension == ExtensionType::Nunchuk);
        m_Snapshot.motion_plus.is_classic_passthrough =
            (m_Snapshot.extension == ExtensionType::ClassicController ||
             m_Snapshot.extension == ExtensionType::ClassicControllerPro);

        // Authoritative "did the passthrough device change" signal while a
        // Motion Plus is present - see m_MotionPlusExtConnected's header
        // comment for why the status report's own bit is no longer used
        // once we get here. First reading after (re)activation just
        // records a baseline rather than firing a spurious re-detect.
        const int8_t now = m_Snapshot.motion_plus.extension_connected ? 1 : 0;
        if (m_MotionPlusExtConnected != -1 && now != m_MotionPlusExtConnected) {
            HandleExtensionChanged();
        }
        m_MotionPlusExtConnected = now;
        return;
    }

    // Otherwise this is the passthrough device's own report. While a
    // MotionPlus is active in passthrough mode, it re-encodes these bytes
    // per WiiBrew's passthrough tables (stolen/relocated LSBs + bookkeeping
    // bits) - decode with the *ViaMotionPlus variant, not the plain one,
    // or an axis LSB gets corrupted and the reserved bits get misread as
    // held-down dpad presses. See WiimoteDecoder.h for byte-level detail.
    //
    // If InitExtension() only got a recognizable ID via the "old way"
    // fallback (m_ExtensionEncrypted), these 6 bytes are just as encrypted
    // - decrypt in place before decoding. Only done on the plain (non-
    // MotionPlus) path: whether a MotionPlus re-encodes an encrypted
    // passthrough device's bytes the same way it does an unencrypted one
    // isn't documented and hasn't been checked against real hardware.
    uint8_t decrypted[6];
    const uint8_t *ext = ee;
    if (m_ExtensionEncrypted && !m_MotionPlusActive) {
        std::memcpy(decrypted, ee, 6);
        DecryptExtensionBytes(decrypted, 6);
        ext = decrypted;
    }

    switch (m_Snapshot.extension) {
        case ExtensionType::Nunchuk:
            m_Snapshot.nunchuk = m_MotionPlusActive
                ? Decode::NunchukViaMotionPlus(ee, 6)
                : Decode::Nunchuk(ext, 6);
            break;
        case ExtensionType::ClassicController:
        case ExtensionType::ClassicControllerPro: {
            const bool is_pro = m_Snapshot.extension == ExtensionType::ClassicControllerPro;
            m_Snapshot.classic = m_MotionPlusActive
                ? Decode::ClassicViaMotionPlus(ee, 6, is_pro)
                : Decode::Classic(ext, 6, is_pro);
            break;
        }
        case ExtensionType::GuitarHeroGuitar:
        case ExtensionType::GuitarHeroDrums: {
            const bool is_drums = m_Snapshot.extension == ExtensionType::GuitarHeroDrums;
            m_Snapshot.guitar = m_MotionPlusActive
                ? Decode::GuitarFromClassic(Decode::ClassicViaMotionPlus(ee, 6, /*is_pro=*/false), is_drums)
                : Decode::Guitar(ext, 6, is_drums);
            break;
        }
        default: break;
    }
}

void WiimoteDevice::DecodeCoreAccelIR12(const uint8_t *buf) {
    // (a1) 33 BB BB AA AA AA II II II II II II II II II II II II
    //       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
    // No extension bytes in this report at all - see SetIRExtendedMode()'s
    // comment for why. nunchuk/classic/guitar are deliberately left
    // untouched here (not zeroed) so they hold their last known values;
    // ir_extended_mode tells callers those values are frozen, not live.
    const uint8_t *bb = buf + 1;
    const uint8_t *aa = buf + 3;
    const uint8_t *ir = buf + 6;

    m_Snapshot.core  = Decode::Buttons(bb);
    m_Snapshot.accel = Decode::Accel(bb, aa);
    m_Snapshot.ir    = Decode::IRExtended(ir);
    m_LastIRReportMs = SDL_GetTicks(); // fed to TickIRWatchdog() - see its comment
    m_Snapshot.ir_possibly_hijacked = false; // this report proves our mode is still in effect right now
}


void WiimoteDevice::DecodeCoreExt19(const uint8_t *buf) {
    // (a1) 34 BB BB EE(x19)  - Balance Board steady-state mode. First 11 of
    // the 19 extension bytes are the weight sensors + temperature + battery
    // (see WiiBrew Wii_Balance_Board#Data_Format); the rest are padding.
    const uint8_t *bb = buf + 1;
    const uint8_t *ee = buf + 3;

    m_Snapshot.core = Decode::Buttons(bb);

    uint8_t ext11[11];
    std::memcpy(ext11, ee, 11);
    BalanceBoardState raw = Decode::BalanceBoard(ext11, m_BalanceCal.value_or(BalanceBoardCalibration{}));

    // Stash the pre-tare reading so TareBalanceBoard() has something to
    // capture, then hand the snapshot the tared version.
    m_BalanceRawKg[0] = raw.kg_top_right;
    m_BalanceRawKg[1] = raw.kg_bottom_right;
    m_BalanceRawKg[2] = raw.kg_top_left;
    m_BalanceRawKg[3] = raw.kg_bottom_left;
    m_BalanceHasRawReading = true;

    ApplyBalanceBoardTare(raw);
    m_Snapshot.balance_board = raw;
    m_Snapshot.balance_board.button_a = m_Snapshot.core.a;
    m_Snapshot.balance_board_tared = (m_BalanceTareKg[0] != 0.f || m_BalanceTareKg[1] != 0.f ||
                                       m_BalanceTareKg[2] != 0.f || m_BalanceTareKg[3] != 0.f);

    CheckBalanceBoardStuckSensors();
}

void WiimoteDevice::ApplyBalanceBoardTare(BalanceBoardState &bb) {
    bb.kg_top_right    -= m_BalanceTareKg[0];
    bb.kg_bottom_right -= m_BalanceTareKg[1];
    bb.kg_top_left     -= m_BalanceTareKg[2];
    bb.kg_bottom_left  -= m_BalanceTareKg[3];
    bb.kg_total = bb.kg_top_right + bb.kg_bottom_right + bb.kg_top_left + bb.kg_bottom_left;

    // Recompute center of gravity from the tared values - same formula as
    // Decode::BalanceBoard(), duplicated here rather than shared because it
    // needs to run on the post-tare numbers, not the raw decode output.
    if (bb.kg_total > 0.01f) {
        const float right = bb.kg_top_right + bb.kg_bottom_right;
        const float left  = bb.kg_top_left  + bb.kg_bottom_left;
        const float front = bb.kg_top_right + bb.kg_top_left;
        const float back  = bb.kg_bottom_right + bb.kg_bottom_left;
        bb.cog_x = (right - left) / bb.kg_total;
        bb.cog_y = (front - back) / bb.kg_total;
    } else {
        bb.cog_x = 0.f;
        bb.cog_y = 0.f;
    }
}

void WiimoteDevice::TareBalanceBoard() {
    if (!m_BalanceHasRawReading) return; // nothing decoded yet - no-op
    for (int i = 0; i < 4; ++i) m_BalanceTareKg[i] = m_BalanceRawKg[i];
    m_Snapshot.balance_board_tared = true;
}

void WiimoteDevice::ClearBalanceBoardTare() {
    for (int i = 0; i < 4; ++i) m_BalanceTareKg[i] = 0.f;
    m_Snapshot.balance_board_tared = false;
}

void WiimoteDevice::SetBalanceBoardTareValues(float top_right, float bottom_right, float top_left, float bottom_left) {
    m_BalanceTareKg[0] = top_right;
    m_BalanceTareKg[1] = bottom_right;
    m_BalanceTareKg[2] = top_left;
    m_BalanceTareKg[3] = bottom_left;
    m_Snapshot.balance_board_tared = (top_right != 0.f || bottom_right != 0.f ||
                                       top_left != 0.f || bottom_left != 0.f);
}

void WiimoteDevice::GetBalanceBoardTareValues(float outKg[4]) const {
    for (int i = 0; i < 4; ++i) outKg[i] = m_BalanceTareKg[i];
}

bool WiimoteDevice::SetIRExtendedMode(bool enabled) {
    if (m_Snapshot.is_balance_board) return false; // no camera hardware
    if (enabled == m_IRExtendedMode) return true;   // already there

    m_IRExtendedMode = enabled;

    // Re-run just the mode-select portion of EnableIRCameraOnce()'s
    // sequence (toggle -> mode write -> toggle), not all 7 steps - the
    // sensitivity blocks aren't mode-dependent, only the data format is
    // changing. Same >=50ms inter-write delay for the same reason: WiiBrew
    // warns writing these back-to-back without a gap risks a
    // half-configured camera state.
    bool ok = true;
    uint8_t toggle08 = 0x08;
    ok &= WriteRegister(Registers::IRModeToggle, &toggle08, 1);
    SDL_Delay(kIRInitStepDelayMs);
    uint8_t mode = enabled ? IRMode::Extended : IRMode::Basic;
    ok &= WriteRegister(Registers::IRMode, &mode, 1);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRModeToggle, &toggle08, 1);
    SDL_Delay(kIRInitStepDelayMs);

    // Re-assert the data reporting mode so the report ID itself switches
    // (0x37 <-> 0x33) - per WiiBrew this is required after any data format
    // change, mirroring what HandleStatusReport()/TickIRWatchdog() already
    // do for other report-mode transitions.
    uint8_t p[2] = {0x04, PreferredReportMode()};
    ok &= SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p, 2);

    if (!ok) {
        LOG_WARN(kTag, "SetIRExtendedMode(%s) had a write failure for %s - "
                        "mode may not have taken effect",
                 enabled ? "on" : "off", m_Path.c_str());
    }

    // Give TickIRWatchdog() a clean slate through the transition, same as
    // a fresh EnableIRCamera() success does - we're switching which report
    // ID carries IR data, and don't want a few transitional milliseconds
    // of silence on the old one misread as a hijack.
    m_LastIRReportMs = 0;
    m_Snapshot.ir_possibly_hijacked = false;
    m_IRReassertAttempts = 0;
    m_Snapshot.ir_extended_mode = enabled;

    LOG_INFO(kTag, "IR Extended mode %s for %s%s", enabled ? "enabled" : "disabled",
             m_Path.c_str(),
             enabled ? " - Nunchuk/Classic/Guitar data is frozen while this is active" : "");

    return ok;
}

void WiimoteDevice::CheckBalanceBoardStuckSensors() {
    // Thresholds are deliberately loose: this only needs to catch the
    // "person is standing on it but 3 corners read 0" pattern from the
    // photo, not fine-grained gently-shifted weight during normal use.
    constexpr float kNearZeroKg   = 0.3f;   // "this corner reads nothing"
    constexpr float kMeaningfulKg = 3.0f;   // "someone/something is on the board"
    constexpr Uint64 kStuckHoldMs = 1500;   // how long the pattern must persist
    constexpr Uint64 kCooldownMs  = 4000;   // don't hammer re-init back-to-back
    constexpr int kMaxAttempts    = 4;      // give up and just flag it after this

    const auto &bb = m_Snapshot.balance_board;
    int near_zero_count = 0;
    if (bb.kg_top_right    < kNearZeroKg) ++near_zero_count;
    if (bb.kg_top_left     < kNearZeroKg) ++near_zero_count;
    if (bb.kg_bottom_right < kNearZeroKg) ++near_zero_count;
    if (bb.kg_bottom_left  < kNearZeroKg) ++near_zero_count;

    const bool looks_stuck = m_BalanceCal.has_value() &&
                              near_zero_count >= 3 &&
                              bb.kg_total >= kMeaningfulKg;

    const Uint64 now = SDL_GetTicks();
    if (!looks_stuck) {
        m_BalanceStuckSinceMs = 0;
        // A clean, all-sensors-alive reading means we recovered (or never
        // had the problem); stop flagging it and reset the attempt count
        // so a *future* occurrence gets the full retry budget again.
        m_Snapshot.balance_board_recovering = false;
        m_BalanceRecoveryAttempts = 0;
        return;
    }

    if (m_BalanceStuckSinceMs == 0) {
        m_BalanceStuckSinceMs = now; // pattern just started
        return;
    }

    if (now - m_BalanceStuckSinceMs < kStuckHoldMs) return; // not persistent yet
    if (now - m_BalanceLastRecoveryAtMs < kCooldownMs) return; // still cooling down
    if (m_BalanceRecoveryAttempts >= kMaxAttempts) {
        // Out of automatic retries - this is the "several power/connect
        // cycles may be necessary" case WiiBrew describes. Leave the flag
        // set so the UI can tell the user to power-cycle the board.
        m_Snapshot.balance_board_recovering = true;
        m_Snapshot.balance_board_recovery_attempts = m_BalanceRecoveryAttempts;
        return;
    }

    // Re-run the extension init dance. This is the same fix WiiBrew
    // documents for their PC interface hitting this exact symptom.
    ++m_BalanceRecoveryAttempts;
    m_BalanceLastRecoveryAtMs = now;
    m_Snapshot.balance_board_recovering = true;
    m_Snapshot.balance_board_recovery_attempts = m_BalanceRecoveryAttempts;
    m_BalanceStuckSinceMs = 0; // give the retry a fresh window to prove itself
    InitExtension();
}

void WiimoteDevice::TickIRWatchdog() {
    // WiiBrew documents that ANY status report - requested or unsolicited
    // - resets the Wiimote's data reporting mode, and the Wiimote answers
    // status requests from whoever sends them, not just us. A second
    // process (in practice, almost always Steam Input, which is documented
    // to open and drive Wiimotes without relinquishing control) will
    // routinely poll it with its own status requests, silently resetting
    // our IR-carrying report mode as a side effect each time. We already
    // react to status reports we ourselves triggered
    // (HandleStatusReport()), but one triggered by someone else's request
    // can win a continuous back-and-forth we only fight reactively.
    //
    // This is a best-effort mitigation, not a fix - we can't make Steam
    // Input relax its grip from inside our own process (see
    // Devices/Wiimote/README.md for the user-facing workaround: Steam's
    // controller_blacklist). What this does is notice when IR data has
    // gone quiet despite us believing it's enabled, and proactively
    // re-assert our report mode rather than waiting for our own next
    // status request (which might not come for a while) - closing the gap
    // to "as fast as this watchdog runs" and flagging the situation for
    // the UI either way.
    if (!m_Snapshot.ir_enabled || m_Snapshot.is_balance_board) return;

    constexpr Uint64 kStaleThresholdMs = 500;  // a healthy link reports far faster than this
    constexpr Uint64 kCooldownMs       = 1000; // don't hammer re-sends back-to-back
    constexpr int kLogEveryNAttempts   = 5;    // periodic re-log cadence, NOT a retry
                                                 // cap - a competing process can keep
                                                 // interfering indefinitely, so we keep
                                                 // re-asserting for as long as it does.

    const Uint64 now = SDL_GetTicks();
    // m_LastIRReportMs == 0 means we've never seen one yet (e.g. right
    // after Init() succeeded, before the first 0x37 has had time to
    // arrive) - don't flag that as hijacked, just wait.
    if (m_LastIRReportMs == 0) return;

    const bool stale = (now - m_LastIRReportMs) > kStaleThresholdMs;
    if (!stale) {
        m_IRReassertAttempts = 0; // healthy again - reset so a future recurrence gets a full budget
        return;
    }

    m_Snapshot.ir_possibly_hijacked = true;

    if (now - m_LastIRReassertAtMs < kCooldownMs) return; // still cooling down

    // Log only on first detection, then periodically (every
    // kLogEveryNAttempts-th re-assert) rather than every cooldown-period
    // re-send, which would spam the log for as long as another process
    // keeps interfering.
    if (m_IRReassertAttempts == 0) {
        LOG_WARN(kTag, "IR data stopped arriving for %s despite IR being enabled - "
                        "another process (commonly Steam Input) may have changed this "
                        "Wiimote's report mode; re-asserting ours. If this repeats, see "
                        "Devices/Wiimote/README.md for how to exclude the device from "
                        "Steam Input's controller_blacklist.", m_Path.c_str());
    } else if (m_IRReassertAttempts % kLogEveryNAttempts == 0) {
        LOG_WARN(kTag, "IR data for %s is still being interfered with after %d re-assert "
                        "attempts - this looks like an ongoing conflict with another "
                        "process, not a one-off glitch.", m_Path.c_str(), m_IRReassertAttempts);
    }

    // Past kLogEveryNAttempts, keep re-asserting at the cooldown's pace
    // rather than stopping - unlike the Balance Board's bounded
    // hardware-quirk retry, a competing process can interfere
    // indefinitely, so periodic re-assertion is the correct steady-state
    // behavior here, not a one-time recovery. No cap on the counter
    // itself, so the modulo check above keeps logging periodically.
    ++m_IRReassertAttempts;
    m_LastIRReassertAtMs = now;

    uint8_t p[2] = {0x04, PreferredReportMode()};
    SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p, 2);
}

} // namespace InputBridge::Wiimote
