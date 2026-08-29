// src/Devices/Wiimote/WiimoteDevice.cpp
#include "WiimoteDevice.h"
#include "WiimoteDecoder.h"
#include "App/Log.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "WiimoteDevice";
constexpr int kReportBufSize = 22; // largest fixed report we consume (0x37 = 22 incl. report ID)
constexpr int kRegisterReadTimeoutMs = 250;

// WiiBrew "IR Camera#Initialization": "To avoid the random state put a
// delay of at least 50ms between every single byte transmission." Used as
// an enforced SDL_Delay() between every write in EnableIRCameraOnce() -
// see that function's comment for why this must be an actual sleep rather
// than relying on incidental Bluetooth/HID scheduling latency.
constexpr Uint32 kIRInitStepDelayMs = 50;
}

WiimoteDevice::WiimoteDevice(SDL_hid_device *dev, std::string hid_path, bool is_balance_board_hint)
    : m_Dev(dev), m_Path(std::move(hid_path)) {
    m_Snapshot.hid_path = m_Path;
    m_Snapshot.is_balance_board = is_balance_board_hint;
    if (m_Dev) SDL_hid_set_nonblocking(m_Dev, 1);
}

WiimoteDevice::~WiimoteDevice() {
    if (m_Dev) SDL_hid_close(m_Dev);
}

// -- Output report helpers -----------------------------------------------

namespace {
// Every output report's first payload byte carries the rumble bit in bit 0.
// `payload` should NOT include the leading report-ID byte - that's added
// here, matching SDL_hid_write's convention of report-ID-as-first-byte.
bool SendReport(SDL_hid_device *dev, uint8_t report_id, bool rumble,
                 const uint8_t *payload, size_t payload_len) {
    if (!dev) return false;
    uint8_t buf[32] = {};
    buf[0] = report_id;
    if (payload && payload_len) std::memcpy(buf + 1, payload, std::min(payload_len, sizeof(buf) - 1));
    if (rumble) buf[1] |= 0x01;
    const size_t total = 1 + std::max<size_t>(payload_len, 1);
    return SDL_hid_write(dev, buf, total) >= 0;
}
} // namespace

bool WiimoteDevice::Init() {
    if (!m_Dev) return false;
    bool ok = true;

    // Ask for a status report so we learn battery + whether an extension is
    // already plugged in before we pick a data-reporting mode.
    {
        uint8_t p[1] = {0x00};
        ok &= SendReport(m_Dev, OutReport::StatusRequest, m_RumbleBit, p, 1);
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
        ok &= SendReport(m_Dev, OutReport::DataReportMode, m_RumbleBit, p, 2);
    }

    // Default to player LED 1 lit so the physical remote shows it's alive.
    SetPlayerLED(1);

    return ok;
}

uint8_t WiimoteDevice::PreferredReportMode() const {
    if (m_Snapshot.is_balance_board) return InReport::CoreExt19;
    return m_IRExtendedMode ? InReport::CoreAccelIR12 : InReport::CoreAccelIR10Ext6;
}

bool WiimoteDevice::EnableIRCamera() {
    // WiiBrew's IR Camera#Initialization section is explicit that this
    // sequence is inherently flaky on real hardware: "After these steps,
    // the Wii Remote will be in one of 3 states: IR camera on but not
    // taking data, IR camera on and taking data at half sensitivity, IR
    // camera on and taking data at full sensitivity. Which state you end
    // up in appears to be pretty much random... To avoid the random state
    // put a delay of at least 50ms between every single byte transmission"
    // - and even then, its own recommendation is "repeat the steps until
    // you're in the desired state", not just "add delays and hope". This
    // implements both halves: an enforced inter-write delay (previously
    // relied on incidental SDL_hid_write/Bluetooth scheduling latency,
    // which is exactly the kind of unenforced timing that produces
    // intermittent failures under different loads/stacks/link quality),
    // and a bounded retry of the whole sequence with a real verification
    // check (status report bit 3, "IR camera enabled") rather than just
    // trusting that 7 writes returning success means the camera is
    // actually producing data.
    //
    // Cost/tradeoff: this runs synchronously inside Init(), itself called
    // once per newly-connected Wiimote from WiimoteManager::Scan() on the
    // main thread - not from the per-frame Poll() loop, so it doesn't cost
    // anything on frames where no new Wiimote just connected, but a worst
    // case of all kMaxAttempts failing is a genuine multi-second stall
    // (kMaxAttempts * (7 writes * kIRInitStepDelayMs + up to
    // kRegisterReadTimeoutMs for verification) - with the values below,
    // up to roughly 1.8s). That's judged an acceptable, infrequent cost
    // for turning "IR camera silently doesn't work about a third of the
    // time" into "IR camera reliably works, connecting takes a bit longer
    // on the unlucky runs" - reducing kMaxAttempts trades reliability back
    // for a shorter worst case if that tradeoff ever needs revisiting.
    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (EnableIRCameraOnce() && VerifyIRCameraEnabled()) {
            m_Snapshot.ir_enabled = true;
            // Give TickIRWatchdog() a clean slate: m_LastIRReportMs == 0
            // means "no IR report seen yet, don't flag as hijacked" until
            // the first real one arrives (which VerifyIRCameraEnabled()'s
            // status-report check does NOT count as - status reports don't
            // carry IR data). Also clear any stale hijack flag/attempt
            // count left over from a previous enable cycle.
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
    // Exhausted retries - leave whatever state the hardware landed in
    // (still probably "on" per WiiBrew's own list of 3 possible outcomes,
    // just possibly not producing data) but don't claim success.
    m_Snapshot.ir_enabled = false;
    LOG_ERROR(kTag, "IR camera failed to enable after %d attempts for %s - "
                     "IR data will not be available this session",
              kMaxAttempts, m_Path.c_str());
    return false;
}

bool WiimoteDevice::EnableIRCameraOnce() {
    bool ok = true;
    uint8_t enable[1] = {0x04};
    ok &= SendReport(m_Dev, OutReport::IRCameraEnable1, m_RumbleBit, enable, 1);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= SendReport(m_Dev, OutReport::IRCameraEnable2, m_RumbleBit, enable, 1);
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
    // Request a fresh status report and block briefly for the reply,
    // mirroring ReadRegister()'s existing spin-wait-with-deadline pattern.
    // Status report byte 3 ("LF") bit 3 (0x08) is WiiBrew-documented as
    // "IR camera enabled" - this is the ground truth for whether the
    // sequence above actually landed in a working state, as opposed to
    // just trusting that every write's own HID-level send succeeded (which
    // per WiiBrew's own account can still randomly land in "on but not
    // taking data").
    //
    // CRITICAL: Init() sends its own fire-and-forget StatusRequest before
    // EnableIRCamera() ever runs (to learn battery/extension state up
    // front), and never reads that reply - it's still sitting in the HID
    // read queue, generated by the Wiimote before the IR camera was ever
    // touched, so its IR-enabled bit is necessarily 0 regardless of how
    // this attempt goes. The same applies to a status reply left over from
    // a PREVIOUS failed attempt in EnableIRCamera()'s retry loop, if one
    // hasn't been fully drained. Bluetooth HID reports are a FIFO queue
    // decoupled from which request "caused" them - SDL_hid_read() has no
    // way to know which reply belongs to which request, so accepting
    // whichever status report arrives first (as an earlier version of this
    // function did) can silently consume one of these stale replies and
    // report a false "not enabled" even when this attempt's sequence
    // genuinely succeeded. Fully drain the queue immediately before
    // sending the request - with nothing else writing to this device
    // concurrently (Init()/EnableIRCamera() run synchronously on one
    // thread), anything already queued at this exact point is guaranteed
    // to predate the request about to be sent, so it's always safe to
    // discard.
    {
        uint8_t drain[kReportBufSize];
        while (SDL_hid_read(m_Dev, drain, sizeof(drain)) > 0) {
            // Discard, except keep buttons fresh from whatever's flushed,
            // same courtesy as the main wait loop below.
            if (drain[0] >= InReport::Core && drain[0] <= InReport::InterleavedB) {
                m_Snapshot.core = Decode::Buttons(drain + 1);
            }
        }
    }

    uint8_t p[1] = {0x00};
    if (!SendReport(m_Dev, OutReport::StatusRequest, m_RumbleBit, p, 1)) return false;

    const Uint64 deadline = SDL_GetTicks() + kRegisterReadTimeoutMs;
    while (SDL_GetTicks() < deadline) {
        uint8_t buf[kReportBufSize] = {};
        const int n = SDL_hid_read(m_Dev, buf, sizeof(buf));
        if (n <= 0) {
            // m_Dev is opened non-blocking, so a "nothing pending" read
            // returns immediately (0), not after waiting for data - without
            // a sleep here this becomes an unthrottled busy-spin calling
            // SDL_hid_read() as fast as the CPU allows for up to
            // kRegisterReadTimeoutMs. Confirmed on Linux/BlueZ in
            // particular: a tight spin like that from a non-realtime
            // userspace thread can starve the Bluetooth stack's own
            // request/response servicing of CPU time, which is directly
            // self-defeating here - the very reply this loop is waiting
            // for can be delayed by the loop's own spinning. A short sleep
            // between empty reads costs negligible latency (worst case
            // adds one sleep interval to how quickly a reply that arrived
            // right after a failed read gets noticed) but avoids
            // pegging a core and competing with the transport it depends on.
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

bool WiimoteDevice::InitExtension() {
    // "New way" init (WiiBrew): write 0x55 -> 0xA400F0, then 0x00 -> 0xA400FB.
    // Works on all known extensions and leaves the ID + data bytes
    // unencrypted, so no decrypt transform is needed anywhere in this file.
    uint8_t v55 = 0x55, v00 = 0x00;
    bool ok = true;
    ok &= WriteRegister(Registers::ExtensionInitNew1, &v55, 1);
    ok &= WriteRegister(Registers::ExtensionInitNew2, &v00, 1);
    if (!ok) return false;

    ExtensionId6 id{};
    if (!ReadRegister(Registers::ExtensionId, 6, id.bytes.data())) {
        m_Snapshot.extension = ExtensionType::None;
    } else {
        m_Snapshot.extension = ClassifyExtension(id);
    }

    // A physical Balance Board's load sensors are wired through the regular
    // extension port and self-identify with type 0x0402, so this branch is
    // sufficient on its own - is_balance_board (the HID product-string
    // hint) is unreliable over Bluetooth (many stacks report a blank or
    // generic product string) and must never gate this. Correct the hint
    // here from the authoritative extension ID so every other is_balance_board
    // check downstream (report mode, IR camera, UI) also self-heals even if
    // enumeration got it wrong.
    if (m_Snapshot.extension == ExtensionType::BalanceBoard) {
        const bool was_already_known = m_Snapshot.is_balance_board;
        m_Snapshot.is_balance_board = true;
        LoadBalanceBoardCalibration();

        // If enumeration's HID-product-string hint missed this (common over
        // Bluetooth, where the string is frequently blank or generic), we
        // only just now learned it's a Balance Board. Init() already ran
        // with the wrong assumption - it will have left the IR camera on
        // and selected the buttons+accel+IR+6ext report mode instead of the
        // 19-ext-byte mode the Balance Board's 11 weight-sensor bytes need.
        // Redo the parts of Init() that depend on this flag now that it's
        // correct, and re-send the data report mode so real weight reports
        // (0x34) start arriving instead of the wrong ones (0x37).
        if (!was_already_known) {
            uint8_t irOff[1] = {0x00};
            SendReport(m_Dev, OutReport::IRCameraEnable1, m_RumbleBit, irOff, 1);
            SendReport(m_Dev, OutReport::IRCameraEnable2, m_RumbleBit, irOff, 1);
            m_Snapshot.ir_enabled = false;
            m_Snapshot.ir = {};

            uint8_t p[2] = {0x04, PreferredReportMode()};
            SendReport(m_Dev, OutReport::DataReportMode, m_RumbleBit, p, 2);
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
    // Activation mode depends on whether something is already plugged into
    // the regular extension port: passthrough keeps that device's data
    // flowing (re-encoded by the MotionPlus) alongside the new gyro bytes;
    // plain activation is used when the extension port is empty.
    uint8_t mode = 0x55;
    uint32_t reg = Registers::MotionPlusInit;
    if (m_Snapshot.extension == ExtensionType::Nunchuk) {
        mode = 0x05;
        reg = Registers::MotionPlusInitNunchukPass;
    } else if (m_Snapshot.extension == ExtensionType::ClassicController ||
               m_Snapshot.extension == ExtensionType::ClassicControllerPro) {
        mode = 0x07;
        reg = Registers::MotionPlusInitClassicPass;
    }
    const bool ok = WriteRegister(reg, &mode, 1);
    m_MotionPlusActive = ok;
    if (ok) {
        m_Snapshot.motion_plus.is_nunchuk_passthrough = (mode == 0x05);
        m_Snapshot.motion_plus.is_classic_passthrough = (mode == 0x07);
    }
    return ok;
}

bool WiimoteDevice::LoadBalanceBoardCalibration() {
    // Plain single read (previous behavior) works on *some* physical
    // boards, but WiiBrew's captured Wii-console init trace shows the real
    // Wii performs a specific "wake" sequence - several writes of 0xAA to
    // register 0xf1, interleaved with reads of the calibration block and a
    // short wait - before trusting the board's 4 weight sensors. Skipping
    // this is a documented, reproducible cause of one or more sensors
    // reading back a constant raw value (and therefore ~0kg after
    // calibration) until the next power/connect cycle; WiiBrew explicitly
    // notes this sequence "is found to correct the problem with disabled
    // weight sensors" in PC-side (non-console) interfaces. Meaning of the
    // 0xf1 writes themselves isn't documented (WiiBrew speculates
    // calibration-related) - this reproduces the trace's shape rather than
    // claiming to explain it.
    uint8_t aa1[1] = {0xAA};
    WriteRegister(Registers::BalanceBoardWake, aa1, 1);
    WriteRegister(Registers::BalanceBoardWake, aa1, 1);
    WriteRegister(Registers::BalanceBoardWake, aa1, 1);

    // One throwaway read of the calibration block's first half at this
    // point, matching the trace's interleaved reads - some boards appear to
    // need a register access in between the initial writes and the final
    // 7-byte burst below to actually start responding on all 4 sensors.
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
    m_BalanceCal = Decode::ParseBalanceBoardCalibration(block);
    return true;
}

// -- Feedback -------------------------------------------------------------

void WiimoteDevice::SetPlayerLED(int player_1to4) {
    const uint8_t bit = uint8_t(0x10 << std::clamp(player_1to4 - 1, 0, 3));
    SetLEDMask(bit);
}

void WiimoteDevice::SetLEDMask(uint8_t mask4bits) {
    uint8_t p[1] = {mask4bits};
    SendReport(m_Dev, OutReport::LEDs, m_RumbleBit, p, 1);
    m_Snapshot.led_mask = mask4bits;
}

void WiimoteDevice::SetRumble(float intensity) {
    m_RumbleIntensity = std::clamp(intensity, 0.0f, 1.0f);
    m_Snapshot.rumble_intensity = m_RumbleIntensity;
    // Restart the PWM period so a fresh SetRumble() call always begins at
    // phase 0 (motor on, for any nonzero intensity) instead of wherever the
    // previous target's cycle happened to be - otherwise a call that lands
    // late in a period could immediately read as "off" for up to
    // kRumblePwmPeriodMs even though the new intensity is nonzero.
    m_RumbleCycleStartMs = SDL_GetTicks();
    UpdateRumblePWM(); // apply immediately rather than waiting for the next Poll()
}

void WiimoteDevice::UpdateRumblePWM() {
    bool desired_bit;
    if (m_RumbleIntensity <= 0.0f) {
        desired_bit = false;
    } else if (m_RumbleIntensity >= 1.0f) {
        desired_bit = true;
    } else {
        const Uint64 now = SDL_GetTicks();
        const Uint64 phase = (now - m_RumbleCycleStartMs) % kRumblePwmPeriodMs;
        const Uint64 on_duration_ms = Uint64(m_RumbleIntensity * float(kRumblePwmPeriodMs));
        desired_bit = phase < on_duration_ms;
    }

    if (desired_bit == m_RumbleBit) return; // no edge to act on - skip the HID write

    m_RumbleBit = desired_bit;
    m_Snapshot.rumble_on = desired_bit;
    // Any report re-asserts the rumble bit; a dedicated Rumble (0x10) report
    // with an otherwise-empty payload is the lightest way to do that on demand.
    uint8_t p[1] = {0x00};
    SendReport(m_Dev, OutReport::Rumble, m_RumbleBit, p, 1);
}

// -- Register read/write (synchronous, bounded wait) ---------------------

bool WiimoteDevice::WriteRegister(uint32_t address, const uint8_t *data, uint8_t size) {
    if (!m_Dev || size > 16) return false;
    uint8_t p[21] = {};
    p[0] = kRegisterFlag; // select control-register space, not EEPROM
    p[1] = uint8_t((address >> 16) & 0xFF);
    p[2] = uint8_t((address >> 8) & 0xFF);
    p[3] = uint8_t(address & 0xFF);
    p[4] = size;
    if (data && size) std::memcpy(p + 5, data, size);
    return SendReport(m_Dev, OutReport::WriteMemory, m_RumbleBit, p, sizeof(p));
}

bool WiimoteDevice::ReadRegister(uint32_t address, uint16_t size, uint8_t *out) {
    if (!m_Dev || !out) return false;

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
        if (!SendReport(m_Dev, OutReport::ReadMemory, m_RumbleBit, p, sizeof(p)))
            return false;

        // Poll for the 0x21 reply, discarding/queuing any regular data
        // reports we happen to read while waiting (Poll() isn't re-entrant
        // with this call, so we do minimal inline handling here: buttons
        // are still safe to decode from any report's first two bytes, but
        // extension/IR handling is skipped for reports consumed here).
        const Uint64 deadline = SDL_GetTicks() + kRegisterReadTimeoutMs;
        bool got = false;
        while (SDL_GetTicks() < deadline) {
            uint8_t buf[kReportBufSize] = {};
            const int n = SDL_hid_read(m_Dev, buf, sizeof(buf));
            if (n <= 0) {
                // Same unthrottled-busy-spin hazard as VerifyIRCameraEnabled()'s
                // wait loop - see its comment. m_Dev is non-blocking, so
                // without this sleep a failed read returns instantly and
                // this loop would call SDL_hid_read() as fast as the CPU
                // allows for up to kRegisterReadTimeoutMs, which on Linux/
                // BlueZ in particular can starve the Bluetooth stack of the
                // CPU time it needs to actually deliver the reply this loop
                // is waiting for.
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
            // Any other report while waiting: at minimum keep buttons fresh.
            if (buf[0] >= InReport::Core && buf[0] <= InReport::InterleavedB) {
                m_Snapshot.core = Decode::Buttons(buf + 1);
            }
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
    if (!m_Dev) return;

    // Keep the rumble PWM's on/off line current every tick, independent of
    // whether any input reports arrived this frame - it has its own timing
    // (kRumblePwmPeriodMs) unrelated to the Wiimote's own report cadence.
    UpdateRumblePWM();

    uint8_t buf[kReportBufSize];
    for (;;) {
        const int n = SDL_hid_read(m_Dev, buf, sizeof(buf));
        if (n <= 0) break; // no more pending reports (non-blocking handle)
        HandleReport(buf, n);
    }

    // If we're waiting for an extension to settle after a connect event,
    // check whether enough time has passed to (re)try identification.
    if (m_ExtensionPendingInit && SDL_GetTicks() >= m_ExtensionSettleAtMs) {
        m_ExtensionPendingInit = false;
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
    const bool was_connected = (m_Snapshot.extension != ExtensionType::None);
    if (ext_connected != was_connected) {
        HandleExtensionChanged();
    }

    // Per WiiBrew: after ANY status report (requested or unsolicited), the
    // reporting mode must be re-sent or no further data reports will arrive.
    uint8_t p[2] = {0x04, PreferredReportMode()};
    SendReport(m_Dev, OutReport::DataReportMode, m_RumbleBit, p, 2);
}

void WiimoteDevice::HandleExtensionChanged() {
    // Give the extension ~150ms to power up / settle before we try to read
    // its ID - reading too early is a common source of misdetection.
    m_ExtensionPendingInit = true;
    m_ExtensionSettleAtMs = SDL_GetTicks() + 150;
    m_Snapshot.extension = ExtensionType::None;
    m_Snapshot.nunchuk = {};
    m_Snapshot.classic = {};
    m_Snapshot.guitar = {};
    m_Snapshot.balance_board = {};
    m_Snapshot.motion_plus = {};
    m_BalanceCal.reset();
    m_MotionPlusPresent = false;
    m_MotionPlusActive = false;
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
    // for the DE data format). Passthrough Nunchuk/Classic data, if any,
    // rides alongside inside the same 6 bytes (some button/axis LSBs are
    // stolen to make room) - re-decoding those precisely is a known gap;
    // for now we surface the MotionPlus gyro data itself, which is the
    // feature being added here.
    if (m_MotionPlusActive && (ee[5] & 0x02)) {
        m_Snapshot.motion_plus = Decode::MotionPlus(ee, 6);
        m_Snapshot.motion_plus.is_nunchuk_passthrough =
            (m_Snapshot.extension == ExtensionType::Nunchuk);
        m_Snapshot.motion_plus.is_classic_passthrough =
            (m_Snapshot.extension == ExtensionType::ClassicController ||
             m_Snapshot.extension == ExtensionType::ClassicControllerPro);
        return;
    }

    switch (m_Snapshot.extension) {
        case ExtensionType::Nunchuk:
            m_Snapshot.nunchuk = Decode::Nunchuk(ee, 6);
            break;
        case ExtensionType::ClassicController:
        case ExtensionType::ClassicControllerPro:
            m_Snapshot.classic = Decode::Classic(ee, 6, m_Snapshot.extension == ExtensionType::ClassicControllerPro);
            break;
        case ExtensionType::GuitarHeroGuitar:
        case ExtensionType::GuitarHeroDrums:
            m_Snapshot.guitar = Decode::Guitar(ee, 6, m_Snapshot.extension == ExtensionType::GuitarHeroDrums);
            break;
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

bool WiimoteDevice::SetIRExtendedMode(bool enabled) {
    if (m_Snapshot.is_balance_board) return false; // no camera hardware
    if (enabled == m_IRExtendedMode) return true;   // already there

    m_IRExtendedMode = enabled;

    // Re-run just the mode-select portion of EnableIRCameraOnce()'s WiiBrew
    // sequence (toggle -> mode write -> toggle) rather than all 7 steps -
    // the sensitivity blocks aren't mode-dependent, only the camera's data
    // format is changing. Same >=50ms inter-write delay (kIRInitStepDelayMs)
    // as the rest of that sequence and for the same reason: WiiBrew warns
    // writing these registers back-to-back without a gap can land the
    // camera in a random half-configured state.
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
    ok &= SendReport(m_Dev, OutReport::DataReportMode, m_RumbleBit, p, 2);

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
    // WiiBrew documents that ANY status report - "requested or
    // unsolicited" - resets the Wiimote's data reporting mode, and the
    // Wiimote answers status requests from WHOEVER sends them, not just
    // us. A second process also talking to the same Wiimote (in practice,
    // almost always Steam Input - it's documented, including in Valve's
    // own bug tracker, to open and actively drive Wiimotes even though
    // they're not an officially supported controller type, and to not
    // relinquish control even when asked) will routinely poll it with its
    // own status requests as part of normal controller-detection/polling
    // behavior. Each one silently resets OUR previously-configured
    // IR-carrying report mode as a side effect, at the firmware level,
    // regardless of which process asked. We already react correctly to
    // status reports WE ourselves triggered (see HandleStatusReport()),
    // but a status reply triggered by someone else's request updates the
    // SAME firmware state without us necessarily reacting fast enough - if
    // the other process is polling aggressively, it can win a continuous
    // back-and-forth we only fight reactively.
    //
    // This is a best-effort mitigation, not a fix: we cannot make Steam
    // Input relax its grip from inside our own process (see
    // Devices/Wiimote/README.md for the user-facing workaround - Steam's
    // controller_blacklist). What this CAN do is notice when IR data has
    // gone quiet despite us believing IR is enabled, and proactively
    // re-assert our report mode rather than waiting to react to our own
    // next status request (which might not come for a while, since we
    // only request status ourselves around connect/extension-change
    // events) - this at least closes the gap to "as fast as this watchdog
    // runs" instead of "whenever we happen to ask again", and flags the
    // situation for the UI either way.
    if (!m_Snapshot.ir_enabled || m_Snapshot.is_balance_board) return;

    constexpr Uint64 kStaleThresholdMs = 500;  // a healthy link reports far faster than this
    constexpr Uint64 kCooldownMs       = 1000; // don't hammer re-sends back-to-back
    constexpr int kLogEveryNAttempts   = 5;    // periodic re-log cadence while this persists -
                                                 // NOT a retry cap (see the loop below): a
                                                 // competing process can keep interfering
                                                 // indefinitely, so we keep re-asserting for as
                                                 // long as that's happening rather than giving up.

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

    // Log only on the first detection and then periodically (every
    // kLogEveryNAttempts-th re-assert) rather than every single cooldown-period
    // re-send, which would otherwise spam the log for as long as another
    // process keeps interfering (potentially the whole session).
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

    // Past kLogEveryNAttempts, keep re-asserting but only at the cooldown's pace
    // (no faster) rather than stopping - unlike the balance board's
    // hardware-quirk retry (bounded, because retrying an already-completed
    // action indefinitely wouldn't help), a competing process can keep
    // interfering indefinitely, so periodically re-asserting for as long
    // as that's happening is the correct steady-state behavior, not a
    // one-time recovery. Keep incrementing past kLogEveryNAttempts too (no
    // cap) purely so the modulo check above can keep logging periodically.
    ++m_IRReassertAttempts;
    m_LastIRReassertAtMs = now;

    uint8_t p[2] = {0x04, PreferredReportMode()};
    SendReport(m_Dev, OutReport::DataReportMode, m_RumbleBit, p, 2);
}


} // namespace InputBridge::Wiimote
