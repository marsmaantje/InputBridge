// src/Devices/Wiimote/WiimoteDevice.cpp
#include "WiimoteDevice.h"
#include "WiimoteDecoder.h"
#include "App/Log.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "WiimoteDevice";
constexpr std::size_t kReportBufSize = 22; // largest fixed report we consume (0x37/0x3e/0x3f = 22 incl. report ID)
constexpr int kRegisterReadTimeoutMs = 250;

// WiiBrew "IR Camera#Initialization": "To avoid the random state put a
// delay of at least 50ms between every single byte transmission." Used as
// an enforced SDL_Delay() between every write in EnableIRCameraOnce() -
// see that function's comment for why this must be an actual sleep rather
// than relying on incidental Bluetooth/HID scheduling latency.
constexpr Uint32 kIRInitStepDelayMs = 50;

// How long to wait after the transport reports IsOpen() before running the
// handshake (Init(), including EnableIRCamera()) - see m_InitSettleAtMs's
// header comment for why a just-opened handle isn't necessarily a
// just-settled Bluetooth connection. 500ms is comfortably longer than the
// sub-second HID-channel negotiation windows observed causing this, while
// still being short enough that a freshly-connected Wiimote feels
// responsive rather than stalled.
constexpr Uint32 kConnectSettleMs = 500;

// std::numbers::pi_v<float> would be nicer but requires C++20 <numbers>;
// avoid relying on M_PI, which isn't defined by <cmath> on MSVC without
// _USE_MATH_DEFINES (this project builds on Windows - see CMakeLists.txt).
constexpr float kPi = 3.14159265358979323846f;
}

WiimoteDevice::WiimoteDevice(std::unique_ptr<IWiimoteTransport> transport, std::string hid_path, bool is_balance_board_hint)
    : m_Transport(std::move(transport)), m_Path(std::move(hid_path)) {
    m_Snapshot.hid_path = m_Path;
    m_Snapshot.is_balance_board = is_balance_board_hint;
    if (m_Transport && m_Transport->IsOpen()) {
        // Nonblocking mode is the transport's job (WiimoteHidTransport /
        // WiimoteL2CAPTransport both open non-blocking) - see
        // WiimoteTransport.h's Read() comment.
        // Don't run Init() synchronously from here (WiimoteManager::Scan()
        // used to call it immediately after construction) - defer it to
        // Poll() once the connection has had kConnectSettleMs to settle.
        // See m_InitSettleAtMs's header comment for why.
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
// report-ID-as-first-byte.
bool SendReport(IWiimoteTransport *transport, std::byte report_id, bool rumble,
                 std::span<const std::byte> payload) {
    if (!transport) return false;
    std::array<std::byte, 32> buf{};
    buf[0] = report_id;
    const std::size_t copied = std::min(payload.size(), buf.size() - 1);
    std::copy_n(payload.begin(), copied, buf.begin() + 1);
    if (rumble) buf[1] |= 0x01_b;
    const std::size_t total = std::min(1 + std::max<std::size_t>(payload.size(), 1), buf.size());
    return transport->Write(std::span<const std::byte>(buf).first(total)) >= 0;
}

// Report bytes 1-2 of any report that starts with the two core-button bytes.
// Caller guarantees `report.size() >= 3` (checked at each call site against
// the byte count the transport actually returned).
CoreButtons ButtonsOf(std::span<const std::byte> report) {
    return Decode::Buttons(report.subspan<1, 2>());
}

// A Read() result of `n` bytes into `buf`, as the span HandleReport() wants.
std::span<const std::byte> Received(const std::array<std::byte, kReportBufSize> &buf, int n) {
    return std::span<const std::byte>(buf).first(std::min(static_cast<std::size_t>(n), buf.size()));
}
} // namespace

bool WiimoteDevice::Init() {
    if (!m_Transport || !m_Transport->IsOpen()) return false;
    bool ok = true;

    // Ask for a status report so we learn battery + whether an extension is
    // already plugged in before we pick a data-reporting mode.
    {
        ok &= SendReport(m_Transport.get(), OutReport::StatusRequest, m_RumbleBit, std::array{0x00_b});
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
        const std::array p{0x04_b, PreferredReportMode()};
        ok &= SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p);
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

std::byte WiimoteDevice::PreferredReportMode() const {
    if (m_Snapshot.is_balance_board) return InReport::CoreExt19;
    switch (m_IRMode) {
        case IRCameraMode::Extended: return InReport::CoreAccelIR12;
        // Full mode alternates between 0x3e and 0x3f on hardware, but the
        // Data Reporting Mode write (Report 0x12) only takes a single MM
        // byte - per WiiBrew, requesting either ID starts the alternating
        // pair, so 0x3e is as good a choice as 0x3f here.
        case IRCameraMode::Full: return InReport::InterleavedA;
        case IRCameraMode::Basic:
        default: return InReport::CoreAccelIR10Ext6;
    }
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
    constexpr std::array enable{0x04_b};
    ok &= SendReport(m_Transport.get(), OutReport::IRCameraEnable1, m_RumbleBit, enable);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= SendReport(m_Transport.get(), OutReport::IRCameraEnable2, m_RumbleBit, enable);
    SDL_Delay(kIRInitStepDelayMs);

    // toggle -> sensitivity block 1 -> block 2 -> mode -> toggle again,
    // each as a register write, each separated by the WiiBrew-recommended
    // >=50ms gap (kIRInitStepDelayMs). WriteRegister() itself is a single
    // blocking HID write (not a read-back), so the delay has to be enforced
    // here explicitly between calls rather than being any part of
    // WriteRegister()'s own timeout/wait logic.
    constexpr std::array toggle08{0x08_b};
    ok &= WriteRegister(Registers::IRModeToggle, toggle08);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRSensitivity1, kIRSensitivityWiiLevel3.block1);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRSensitivity2, kIRSensitivityWiiLevel3.block2);
    SDL_Delay(kIRInitStepDelayMs);
    const std::array mode{
        m_IRMode == IRCameraMode::Full ? IRMode::Full :
        m_IRMode == IRCameraMode::Extended ? IRMode::Extended : IRMode::Basic};
    ok &= WriteRegister(Registers::IRMode, mode);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRModeToggle, toggle08);
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
    // decoupled from which request "caused" them - IWiimoteTransport::Read()
    // has no way to know which reply belongs to which request, so accepting
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
        std::array<std::byte, kReportBufSize> drain{};
        for (int n = m_Transport->Read(drain); n > 0; n = m_Transport->Read(drain)) {
            // Discard, except keep buttons fresh from whatever's flushed,
            // same courtesy as the main wait loop below.
            if (n >= 3 && drain[0] >= InReport::Core && drain[0] <= InReport::InterleavedB) {
                m_Snapshot.core = ButtonsOf(drain);
            }
        }
    }

    if (!SendReport(m_Transport.get(), OutReport::StatusRequest, m_RumbleBit, std::array{0x00_b})) return false;

    const Uint64 deadline = SDL_GetTicks() + kRegisterReadTimeoutMs;
    while (SDL_GetTicks() < deadline) {
        std::array<std::byte, kReportBufSize> buf{};
        const int n = m_Transport->Read(buf);
        if (n <= 0) {
            // The transport is non-blocking (see IWiimoteTransport::Read),
            // so a "nothing pending" read returns immediately (0), not
            // after waiting for data - without a sleep here this becomes
            // an unthrottled busy-spin calling Read() as fast as the CPU
            // allows for up to kRegisterReadTimeoutMs. Confirmed on Linux/BlueZ in
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
            HandleStatusReport(Received(buf, n));
            const bool ir_bit = (buf[3] & 0x08_b) != 0x00_b;
            LOG_VERBOSE(kTag, "IR verification status reply: LF=0x%02x -> IR bit %s",
                        std::to_integer<unsigned>(buf[3]), ir_bit ? "SET" : "clear");
            return ir_bit;
        }
        // Anything else arriving while we wait: at minimum keep buttons
        // fresh, matching ReadRegister()'s same fallback.
        if (n >= 3 && buf[0] >= InReport::Core && buf[0] <= InReport::InterleavedB) {
            m_Snapshot.core = ButtonsOf(buf);
        }
    }
    LOG_WARN(kTag, "Timed out waiting for status reply during IR verification for %s", m_Path.c_str());
    return false; // timed out waiting for the status reply
}

bool WiimoteDevice::InitExtension() {
    // "New way" init (WiiBrew): write 0x55 -> 0xA400F0, then 0x00 -> 0xA400FB.
    // Works on all known official extensions and leaves the ID + data bytes
    // unencrypted, so try it first - it needs no per-byte decrypt step.
    bool ok = true;
    ok &= WriteRegister(Registers::ExtensionInitNew1, std::array{0x55_b});
    ok &= WriteRegister(Registers::ExtensionInitNew2, std::array{0x00_b});
    if (!ok) return false;

    m_ExtensionEncrypted = false;

    ExtensionId6 id{};
    if (!ReadRegister(Registers::ExtensionId, id.bytes)) {
        m_Snapshot.extension = ExtensionType::None;
    } else {
        ExtensionType classified = ClassifyExtension(id);
        if (classified == ExtensionType::Unknown) {
            // The "new way" write didn't produce a recognizable ID. Some
            // wireless/third-party Nunchuks either ignore that write or
            // ship with encryption on and no way to disable it - fall back
            // to the "old way" init (write 0x00 -> 0xA400F0 only, leaving
            // encryption ON) and decrypt the ID bytes before classifying.
            // See WiiBrew's Nunchuk page, "Wireless Nunchuks" section, and
            // WiimoteProtocol.h's DecryptExtensionByte().
            if (WriteRegister(Registers::ExtensionInitOld, std::array{0x00_b}) &&
                ReadRegister(Registers::ExtensionId, id.bytes)) {
                DecryptExtensionBytes(id.bytes);
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
            constexpr std::array irOff{0x00_b};
            SendReport(m_Transport.get(), OutReport::IRCameraEnable1, m_RumbleBit, irOff);
            SendReport(m_Transport.get(), OutReport::IRCameraEnable2, m_RumbleBit, irOff);
            m_Snapshot.ir_enabled = false;
            m_Snapshot.ir = {};

            const std::array p{0x04_b, PreferredReportMode()};
            SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p);
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
    if (!ReadRegister(Registers::MotionPlusId, id.bytes)) {
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
    std::byte mode = 0x04_b;
    uint32_t reg = Registers::MotionPlusInit;
    if (m_Snapshot.extension == ExtensionType::Nunchuk) {
        mode = 0x05_b;
        reg = Registers::MotionPlusInitNunchukPass;
    } else if (m_Snapshot.extension == ExtensionType::ClassicController ||
               m_Snapshot.extension == ExtensionType::ClassicControllerPro) {
        mode = 0x07_b;
        reg = Registers::MotionPlusInitClassicPass;
    }

    // Prime the ID block first (0x55 -> 0xA600F0), same as retail games do,
    // before selecting the mode. Without this, a unit that's still in a
    // previously-activated state (e.g. it was standalone and a Nunchuk just
    // got plugged in) can ignore the mode write below.
    WriteRegister(Registers::MotionPlusPrime, std::array{0x55_b});

    const bool ok = WriteRegister(reg, std::array{mode});
    m_MotionPlusActive = ok;
    if (ok) {
        m_Snapshot.motion_plus.is_nunchuk_passthrough = (mode == 0x05_b);
        m_Snapshot.motion_plus.is_classic_passthrough = (mode == 0x07_b);
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
    constexpr std::array aa1{0xAA_b};
    WriteRegister(Registers::BalanceBoardWake, aa1);
    WriteRegister(Registers::BalanceBoardWake, aa1);
    WriteRegister(Registers::BalanceBoardWake, aa1);

    // One throwaway read of the calibration block's first half at this
    // point, matching the trace's interleaved reads - some boards appear to
    // need a register access in between the initial writes and the final
    // 7-byte burst below to actually start responding on all 4 sensors.
    std::array<std::byte, 16> discard{};
    ReadRegister(Registers::ExtensionCalib, discard);

    // The trace's "Write f1: aa aa aa 55 aa aa aa" burst - a single write
    // spanning 7 bytes, not 7 separate single-byte writes (single-byte
    // writes of just 0xAA were already sent above; this is the distinct
    // longer write that follows in the captured sequence).
    constexpr std::array burst{0xAA_b, 0xAA_b, 0xAA_b, 0x55_b, 0xAA_b, 0xAA_b, 0xAA_b};
    WriteRegister(Registers::BalanceBoardWake, burst);

    WriteRegister(Registers::BalanceBoardWake, aa1);
    WriteRegister(Registers::BalanceBoardWake, aa1);

    // The trace waits here before the sensors settle; 50ms is a
    // conservative margin over what's needed in practice without adding
    // noticeable connect-time latency.
    SDL_Delay(50);

    WriteRegister(Registers::BalanceBoardWake, aa1);

    std::array<std::byte, 32> block{};
    if (!ReadRegister(Registers::ExtensionCalib, block)) return false;

    // Reference Temperature + the unknown byte after it (0xA40060/61) - not
    // part of the 32-byte block above, but required to reproduce the
    // board's CRC32 (see ParseBalanceBoardCalibration()). If this read
    // fails, fall through with zeroed bytes; that just means the CRC won't
    // match (extremely unlikely to accidentally match) and we correctly
    // treat it as a bad read rather than risk misreporting a real failure
    // as a corrupted calibration block.
    std::array<std::byte, 2> ref_temp{};
    ReadRegister(Registers::ExtensionCalibRefTemp, ref_temp);

    BalanceBoardCalibration parsed = Decode::ParseBalanceBoardCalibration(block, ref_temp);
    if (!parsed.valid) {
        // CRC32 mismatch: the read was corrupted. Don't clobber whatever
        // calibration we already have (if any) with bad/zeroed data - keep
        // using the last known-good calibration and let a future call to
        // this function (e.g. on reconnect) get a clean read instead.
        return false;
    }
    m_BalanceCal = parsed;
    return true;
}

// -- Feedback -------------------------------------------------------------

void WiimoteDevice::SetPlayerLED(int player_1to4) {
    const auto bit = static_cast<uint8_t>(0x10u << std::clamp(player_1to4 - 1, 0, 3));
    SetLEDMask(bit);
}

void WiimoteDevice::SetLEDMask(uint8_t mask4bits) {
    SendReport(m_Transport.get(), OutReport::LEDs, m_RumbleBit, std::array{static_cast<std::byte>(mask4bits)});
    m_Snapshot.led_mask = mask4bits;
}

void WiimoteDevice::SetRumble(float intensity) {
    m_RumbleIntensity = std::clamp(intensity, 0.0f, 1.0f);
    m_Snapshot.rumble_intensity = m_RumbleIntensity;
    // Restart the PWM period so a fresh SetRumble() call always begins at
    // phase 0 (motor on, for any nonzero intensity) instead of wherever the
    // previous target's cycle happened to be - otherwise a call that lands
    // late in a period could immediately read as "off" for up to
    // kRumblePwmPeriodMs even though the new intensity is nonzero. Also
    // drop any duty-cycle debt from the previous target - it doesn't mean
    // anything relative to the new intensity.
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
        // How long since we last got a chance to check/toggle the bit at
        // all. Under normal conditions (Poll() running faster than the
        // carrier period) this is a few ms and everything below is a
        // no-op - phase-in-period alone decides the bit, same as before.
        // A frame hitch, the app losing focus/being throttled, or the
        // Bluetooth stack stalling delivery of everything (not just
        // rumble) all show up here the same way: as one bigger-than-usual
        // gap. If that gap spans one or more WHOLE carrier periods, we
        // know for certain the line sat wherever it last was (100% on or
        // 100% off) for those periods rather than tracking `intensity` -
        // there was no Poll() call in between to correct it. Rather than
        // silently accepting that as lost accuracy, credit/debit the
        // resulting shortfall or excess into m_RumbleDutyDebtMs and pay it
        // back by nudging the CURRENT period's on/off boundary.
        const Uint64 gapMs = (m_RumbleLastPollMs == 0) ? 0 : (now - m_RumbleLastPollMs);
        const Uint64 skippedPeriods = gapMs / kRumblePwmPeriodMs;
        if (skippedPeriods > 0) {
            const float targetOnPerSkippedMs    = m_RumbleIntensity * float(kRumblePwmPeriodMs);
            const float deliveredOnPerSkippedMs = m_RumbleBit ? float(kRumblePwmPeriodMs) : 0.0f;
            m_RumbleDutyDebtMs += float(skippedPeriods) * (targetOnPerSkippedMs - deliveredOnPerSkippedMs);

            // Bound the debt so a long stall (app suspended, a multi-
            // second hitch) can't demand an absurdly long unbroken on/off
            // burst once polling resumes - cap at a few periods' worth of
            // correction and let any remainder just be lost, the same as
            // it would have been without this mechanism at all.
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
    SendReport(m_Transport.get(), OutReport::Rumble, m_RumbleBit, std::array{0x00_b});
}

// -- Speaker ---------------------------------------------------------------

bool WiimoteDevice::EnableSpeaker(uint32_t sample_rate_hz, uint8_t volume, SpeakerAudioFormat format) {
    if (!m_Transport || !m_Transport->IsOpen()) return false;
    if (sample_rate_hz == 0) sample_rate_hz = 2000;

    // ADPCM4's hardware volume register only goes to 0x40 (WiiBrew's
    // Speaker Configuration section) - clamp rather than writing an
    // out-of-range value whose hardware behavior isn't documented.
    if (format == SpeakerAudioFormat::ADPCM4 && volume > 0x40) volume = 0x40;

    // A live format switch leaves anything already queued in the old
    // format meaningless (PCM8 bytes played back as ADPCM4 nibbles, or
    // vice versa, is just noise) - see QueueADPCM4()'s comment for the
    // fuller rationale, which applies here too since this discards the
    // queue and resets the ADPCM encoder exactly like StopSpeaker() does.
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

    ok &= SendReport(m_Transport.get(), OutReport::SpeakerEnable, m_RumbleBit, std::array{0x04_b});
    ok &= SendReport(m_Transport.get(), OutReport::SpeakerMute, m_RumbleBit, std::array{0x04_b});
    ok &= WriteRegister(Registers::SpeakerInitFlag, std::array{0x01_b});
    ok &= WriteRegister(Registers::SpeakerConfig, std::array{0x08_b});

    // rate register value = clock / desired Hz (WiiBrew's formula; integer
    // division, so the achieved rate may differ slightly from what's asked
    // for). Each format has its own clock (kSpeakerPcmClockHz /
    // kSpeakerAdpcmClockHz, WiimoteProtocol.h) - using the wrong one here
    // would silently configure a rate 2x off from what was asked for.
    const bool is_adpcm = format == SpeakerAudioFormat::ADPCM4;
    const uint32_t clock_hz = is_adpcm ? kSpeakerAdpcmClockHz : kSpeakerPcmClockHz;
    const uint32_t rate_value = clock_hz / sample_rate_hz;
    const std::array<std::byte, 7> config{
        0x00_b,                                // unknown, always 0x00 per WiiBrew
        is_adpcm ? SpeakerFormat::Adpcm4 : SpeakerFormat::Pcm8,
        LowByte(rate_value),                   // sample rate, little-endian
        LowByte(rate_value >> 8),
        static_cast<std::byte>(volume),        // 0x00-0xFF (PCM8) / 0x00-0x40 (ADPCM4), already clamped above
        0x00_b, 0x00_b,                        // unknown, always 0x00 per WiiBrew
    };
    ok &= WriteRegister(Registers::SpeakerConfig, config);

    ok &= WriteRegister(Registers::SpeakerCommitFlag, std::array{0x01_b});

    ok &= SendReport(m_Transport.get(), OutReport::SpeakerMute, m_RumbleBit, std::array{0x00_b});

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
    // every (chunk_samples / sample_rate_hz) seconds - i.e. we hand the
    // Wiimote new data about as fast as it's consuming the last chunk,
    // not faster (which would just pile up in whatever's buffer) or
    // slower (which would starve it, producing audible dropouts/pops).
    // ADPCM4 packs 2 samples/byte where PCM8 packs 1, so the same
    // kSpeakerMaxChunkBytes chunk covers twice as many samples (i.e.
    // twice the playback time) in ADPCM4 - using the PCM8 math here would
    // silently pace transmission at half the rate the hardware is
    // actually consuming ADPCM4 data, starving it.
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
    if (m_Transport && m_Transport->IsOpen()) {
        SendReport(m_Transport.get(), OutReport::SpeakerEnable, m_RumbleBit, std::array{0x00_b});
    }
    m_SpeakerEnabled = false;
    m_SpeakerSampleRateHz = 0;
    m_SpeakerVolume = 0;
    StopSpeaker();
}

void WiimoteDevice::QueuePCM8(const int8_t *samples, size_t count) {
    if (!samples || !count) return;
    // Signed 8-bit PCM goes on the wire as the same bit pattern, so
    // reinterpret each sample's two's-complement value as a raw byte.
    m_SpeakerQueue.reserve(m_SpeakerQueue.size() + count);
    for (const int8_t sample : std::span<const int8_t>(samples, count)) {
        m_SpeakerQueue.push_back(static_cast<std::byte>(static_cast<uint8_t>(sample)));
    }
}

void WiimoteDevice::QueueADPCM4(const int16_t *samples, size_t count) {
    if (!samples || !count) return;
    std::vector<std::byte> packed;
    m_AdpcmEncoder.Encode(samples, count, packed);
    if (packed.empty()) return;
    // Already raw wire bytes (TickSpeaker() copies them straight into the
    // report), so append as-is.
    m_SpeakerQueue.insert(m_SpeakerQueue.end(), packed.begin(), packed.end());
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
    // Bluetooth link fed at that format's higher per-sample cost, 3000Hz -
    // its "standard value" - for ADPCM4) - see SpeakerAudioFormat's
    // comment for why picking the wrong one for the format is exactly
    // what makes a "beep" sound like an aliased buzz instead of a tone.
    if (sample_rate_hz == 0) {
        sample_rate_hz = (format == SpeakerAudioFormat::ADPCM4) ? 3000 : 2000;
    }

    // Volume 0 means "play nothing" - see TickSpeaker()'s comment on why
    // silence is enforced by never sending data rather than by trusting
    // the hardware volume register. No point running the enable sequence
    // or generating samples that TickSpeaker() would just discard anyway.
    if (volume == 0) {
        StopSpeaker();
        return;
    }
    if (format == SpeakerAudioFormat::ADPCM4 && volume > 0x40) volume = 0x40; // see EnableSpeaker()

    // Only re-run the (synchronous, several-HID-writes) enable sequence if
    // something it actually controls has changed - rate, volume, or
    // format - so repeated same-settings beeps (e.g. a UI click sound)
    // can queue back-to-back without re-doing the register dance and its
    // associated mute/unmute click every time. Checking rate/volume alone
    // here was a past bug (see git history): a repeat call with a new
    // `volume` but the same (default) rate silently skipped
    // EnableSpeaker() and kept whatever volume was set on the very first
    // call, making `volume` appear to do nothing - format needs the same
    // treatment, or switching formats between beeps would silently keep
    // encoding/queueing in the old one.
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
    if (!m_SpeakerEnabled || !m_Transport || !m_Transport->IsOpen()) return;

    // Treat volume 0 as "play nothing" rather than trusting the hardware
    // gain register (VV) to produce true silence at its documented
    // minimum - WiiBrew itself notes "the full purpose of these bytes is
    // not known", and real hardware has been confirmed to still output
    // audible sound at VV=0x00. Dropping the queue without transmitting
    // anything is a guarantee the register-level behavior isn't; anything
    // still queued when volume drops to 0 (e.g. via the UI slider mid-
    // playback) is discarded rather than silently sent anyway.
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

    // Send every chunk whose scheduled time has already passed, not just
    // one. Poll() runs at whatever the host's frame/tick rate is (commonly
    // ~16.67ms at 60fps), which can be SLOWER than the ~10ms/20-byte
    // cadence 2000Hz 8-bit PCM actually needs - sending only one chunk per
    // Poll() call in that case silently under-delivers (e.g. 60 chunks/sec
    // instead of the ~100/sec required), starving the speaker's buffer
    // between writes. That starvation is what crackle/stutter sounds like
    // on this hardware, not a bad waveform - the fix is catching up here,
    // not changing what gets generated. Bounded (kMaxChunksPerTick) so a
    // real stall (window unfocused, debugger pause, device hiccup) can't
    // dump an unbounded backlog into one burst of HID writes.
    constexpr int kMaxChunksPerTick = 8;
    int sent = 0;
    while (m_SpeakerQueuePos < m_SpeakerQueue.size() &&
           now >= m_SpeakerNextChunkAtMs &&
           sent < kMaxChunksPerTick) {
        const size_t remaining = m_SpeakerQueue.size() - m_SpeakerQueuePos;
        const std::size_t n = std::min<std::size_t>(remaining, kSpeakerMaxChunkBytes);

        // Report 0x18 payload is always the full LL byte + 20 data bytes,
        // even for a short final chunk - SendReport()'s zero-initialized
        // buf[32] already leaves any bytes past `n` as padding zeroes.
        std::array<std::byte, 1 + kSpeakerMaxChunkBytes> p{};
        p[0] = LowByte(static_cast<uint32_t>(n << 3)); // LL: length, shifted left 3 bits (WiiBrew)
        std::ranges::copy(std::span<const std::byte>(m_SpeakerQueue).subspan(m_SpeakerQueuePos, n),
                          p.begin() + 1);
        SendReport(m_Transport.get(), OutReport::SpeakerData, m_RumbleBit, p);

        m_SpeakerQueuePos += n;
        // Schedule from where the PREVIOUS chunk was due, not from `now` -
        // advancing from `now` each time would silently let real delivery
        // rate drift below the target rate under any sustained Poll()
        // jitter, reintroducing the same starvation this loop exists to
        // fix.
        m_SpeakerNextChunkAtMs += m_SpeakerChunkIntervalMs;
        ++sent;
    }

    // If we're still behind after kMaxChunksPerTick catch-up sends (a
    // stall long enough that even the bounded burst above couldn't clear
    // it), resync the schedule to now rather than leaving it arbitrarily
    // far in the past - otherwise every future Tick would think it's
    // perpetually catching up and burst-send indefinitely.
    if (now >= m_SpeakerNextChunkAtMs)
        m_SpeakerNextChunkAtMs = now + m_SpeakerChunkIntervalMs;
}

// -- Register read/write (synchronous, bounded wait) ---------------------

bool WiimoteDevice::WriteRegister(uint32_t address, std::span<const std::byte> data) {
    if (!m_Transport || !m_Transport->IsOpen() || data.size() > 16) return false;
    std::array<std::byte, 21> p{};
    p[0] = kRegisterFlag; // select control-register space, not EEPROM
    p[1] = LowByte(address >> 16);
    p[2] = LowByte(address >> 8);
    p[3] = LowByte(address);
    p[4] = static_cast<std::byte>(data.size());
    std::ranges::copy(data, p.begin() + 5);
    return SendReport(m_Transport.get(), OutReport::WriteMemory, m_RumbleBit, p);
}

bool WiimoteDevice::ReadRegister(uint32_t address, std::span<std::byte> out) {
    if (!m_Transport || !m_Transport->IsOpen() || out.empty()) return false;

    std::size_t remaining = out.size();
    std::size_t offset = 0;
    uint32_t addr = address;

    while (remaining > 0) {
        const auto chunk = static_cast<uint32_t>(std::min<std::size_t>(remaining, 16));

        const std::array<std::byte, 6> p{
            kRegisterFlag,
            LowByte(addr >> 16), LowByte(addr >> 8), LowByte(addr),
            LowByte(chunk >> 8), LowByte(chunk),
        };
        if (!SendReport(m_Transport.get(), OutReport::ReadMemory, m_RumbleBit, p))
            return false;

        // Poll for the 0x21 reply. Regular data reports that arrive while
        // we wait are run through the normal HandleReport() decoder (safe
        // to call here - it never calls back into ReadRegister()) so
        // accel/IR/extension/motion_plus stay live for the whole duration
        // of the read instead of freezing at their pre-read values; only
        // buttons used to be kept fresh here, which stalled everything
        // else for up to kRegisterReadTimeoutMs per chunk (worse for
        // multi-chunk reads like the 32-byte calibration blocks).
        const Uint64 deadline = SDL_GetTicks() + kRegisterReadTimeoutMs;
        bool got = false;
        while (SDL_GetTicks() < deadline) {
            std::array<std::byte, kReportBufSize> buf{};
            const int n = m_Transport->Read(buf);
            if (n <= 0) {
                // Same unthrottled-busy-spin hazard as VerifyIRCameraEnabled()'s
                // wait loop - see its comment. The transport is non-blocking,
                // so without this sleep a failed read returns instantly and
                // this loop would call Read() as fast as the CPU
                // allows for up to kRegisterReadTimeoutMs, which on Linux/
                // BlueZ in particular can starve the Bluetooth stack of the
                // CPU time it needs to actually deliver the reply this loop
                // is waiting for.
                SDL_Delay(1);
                continue;
            }
            if (buf[0] == InReport::ReadMemoryData) {
                // (a1) 21 BB BB SE FF FF DD..DD
                const unsigned se = std::to_integer<unsigned>(buf[3]);
                const unsigned err = se & 0x0Fu;
                const std::size_t got_size = (se >> 4) + 1u;
                if (err != 0) return false; // read from nonexistent/write-only register
                [[maybe_unused]] const uint16_t got_addr_lo = ToU16BE(buf[4], buf[5]); // available for stricter validation if desired
                const std::size_t n_copy = std::min<std::size_t>(got_size, chunk);
                std::copy_n(buf.begin() + 6, n_copy, out.begin() + static_cast<std::ptrdiff_t>(offset));
                got = true;
                break;
            }
            // Any other report while waiting: decode it exactly like Poll()
            // would, so nothing goes stale just because a register read is
            // in flight.
            HandleReport(Received(buf, n));
        }
        if (!got) return false;

        offset += chunk;
        addr += chunk;
        remaining -= chunk;
    }
    return true;
}

// -- Input report handling -----------------------------------------------

void WiimoteDevice::Poll() {
    if (!m_Transport || !m_Transport->IsOpen()) return;

    // Keep the rumble PWM's on/off line current every tick, independent of
    // whether any input reports arrived this frame - it has its own timing
    // (kRumblePwmPeriodMs) unrelated to the Wiimote's own report cadence.
    UpdateRumblePWM();
    // Same "own timing, unrelated to report cadence" rationale as
    // UpdateRumblePWM() above - see TickSpeaker()'s declaration comment.
    TickSpeaker();

    std::array<std::byte, kReportBufSize> buf{};
    for (;;) {
        const int n = m_Transport->Read(buf);
        if (n <= 0) break; // no more pending reports (non-blocking handle)
        HandleReport(Received(buf, n));
    }

    // Run the deferred handshake once the connection has had a moment to
    // settle - see m_InitSettleAtMs's header comment. Checked before the
    // extension-settle handling below since Init() (via EnableIRCamera())
    // sends its own StatusRequest, and InitExtension() being triggered off
    // that status report's reply is expected to still work the same way it
    // always has once Init() actually runs.
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
    // real deadline. Also guarded on !m_ExtensionPendingInit: when an
    // extension-changed event just fired, InitExtension() is already
    // queued to run (and it calls DetectMotionPlus() itself once it does)
    // - firing this independent probe first would race ahead of it with
    // m_Snapshot.extension still reset to None, picking standalone (0x04)
    // activation even behind a Nunchuk/Classic Controller that hasn't been
    // identified yet. InitExtension() running right after would then tear
    // that down again via its regular-extension-port init (see the retry
    // block's comment below for why that deactivates an active Motion
    // Plus), producing a detect/lose/redetect loop instead of a clean
    // single activation. Stops re-arming (and re-reading the register)
    // once a Motion Plus is actually found.
    if (!m_InitPending && !m_ExtensionPendingInit && !m_MotionPlusPresent && !m_Snapshot.is_balance_board &&
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
    // InitExtension(), whose reset writes to 0xA400F0/FB are exactly the
    // sequence WiiBrew documents as deactivating an active Motion Plus via
    // that same register swap. Without this guard, an active standalone
    // Motion Plus gets silently deactivated ~1s after every (re)activation,
    // and one behind a Nunchuk/Classic Controller flip-flops between
    // passthrough device and Motion Plus as each retry knocks it back down
    // and DetectMotionPlus()'s own re-probe (at the end of InitExtension())
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

void WiimoteDevice::HandleReport(std::span<const std::byte> report) {
    if (report.empty()) return;
    const std::size_t len = report.size();
    m_Snapshot.connected = true;
    m_Snapshot.last_report_ms = SDL_GetTicks();
    switch (report[0]) {
        case InReport::Status:
            HandleStatusReport(report);
            break;
        case InReport::CoreAccelIR10Ext6:
            if (len >= 22) DecodeCoreAccelIR10Ext6(report);
            break;
        case InReport::CoreAccelIR12:
            if (len >= 18) DecodeCoreAccelIR12(report);
            break;
        case InReport::CoreExt19:
            if (len >= 22) DecodeCoreExt19(report);
            break;
        case InReport::InterleavedA:
        case InReport::InterleavedB:
            if (len >= 22) DecodeInterleavedIR(report);
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
            if (report[0] != InReport::Ext21 && len >= 3) {
                m_Snapshot.core = ButtonsOf(report);
            }
            break;
    }
}

void WiimoteDevice::HandleStatusReport(std::span<const std::byte> report) {
    // (a1) 20 BB BB LF 00 00 VV
    if (report.size() < 7) return; // truncated - nothing safe to read
    m_Snapshot.core = ButtonsOf(report);
    const std::byte lf = report[3];
    const auto battery = std::to_integer<uint8_t>(report[6]);
    m_Snapshot.battery = ClassifyWiimoteBattery(battery);

    const bool ext_connected = (lf & 0x02_b) != 0x00_b;
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
    const std::array p{0x04_b, PreferredReportMode()};
    SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p);
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
    m_MotionPlusExtConnectedStableCount = 0;
    // Let Poll()'s bare-Motion-Plus probe (see m_MotionPlusNextProbeAtMs's
    // header comment) run again on its next tick instead of possibly
    // waiting out whatever was left of the previous ~8s window - relevant
    // e.g. right after a Nunchuk is unplugged from behind a Motion Plus.
    m_MotionPlusNextProbeAtMs = 0;
}

void WiimoteDevice::DecodeCoreAccelIR10Ext6(std::span<const std::byte> report) {
    // (a1) 37 BB BB AA AA AA II II II II II II II II II II EE EE EE EE EE EE
    //       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21
    // Caller (HandleReport) has already checked report.size() >= 22.
    const auto bb = report.subspan<1, 2>();
    const auto aa = report.subspan<3, 3>();
    const auto ir = report.subspan<6, 10>();
    const auto ee = report.subspan<16, 6>();

    m_Snapshot.core  = Decode::Buttons(bb);
    m_Snapshot.accel = Decode::Accel(bb, aa);
    m_Snapshot.ir    = Decode::IRBasic(ir);
    m_LastIRReportMs = SDL_GetTicks(); // fed to TickIRWatchdog() - see its comment
    m_Snapshot.ir_possibly_hijacked = false; // this report proves our mode is still in effect right now

    // Once active, the MotionPlus takes over the extension byte slot: its
    // own gyro data is distinguished from a regular extension's data by
    // ee[5] bit 1 == 1 (the "extension identifier" bit WiiBrew documents
    // for the DE data format).
    if (m_MotionPlusActive && (ee[5] & 0x02_b) != 0x00_b) {
        m_Snapshot.motion_plus = Decode::MotionPlus(ee);
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
        // Debounced (see m_MotionPlusExtConnectedStableCount's comment):
        // a differing reading only counts once it's held for several
        // reports in a row, so a single noisy sample can't fire this.
        const int8_t now = m_Snapshot.motion_plus.extension_connected ? 1 : 0;
        if (m_MotionPlusExtConnected == -1) {
            m_MotionPlusExtConnected = now;
            m_MotionPlusExtConnectedStableCount = 0;
        } else if (now == m_MotionPlusExtConnected) {
            m_MotionPlusExtConnectedStableCount = 0;
        } else if (++m_MotionPlusExtConnectedStableCount >= kMotionPlusExtConnectedDebounce) {
            m_MotionPlusExtConnected = now;
            m_MotionPlusExtConnectedStableCount = 0;
            HandleExtensionChanged();
        }
        return;
    }

    // Otherwise this is the passthrough device's own report. While the
    // MotionPlus is active in a passthrough mode, it has re-encoded these
    // bytes per WiiBrew's passthrough tables (stolen/relocated LSBs +
    // bookkeeping bits) - decode with the *ViaMotionPlus variant, not the
    // plain one, or an axis LSB gets corrupted and the always-zero
    // discriminator/reserved bits get misread as held-down dpad presses.
    // See WiimoteDecoder.h for the byte-level detail.
    //
    // If InitExtension() only got a recognizable ID via the "old way"
    // fallback (m_ExtensionEncrypted), these 6 bytes are the extension's
    // live data and are just as encrypted as the ID was - decrypt in place
    // before handing them to any decoder. Only done on the plain (non-
    // MotionPlus) path: whether a MotionPlus re-encodes an *encrypted*
    // passthrough device's bytes the same way it does an unencrypted one
    // isn't documented on WiiBrew and hasn't been checked against real
    // hardware, so left alone here rather than guessed at.
    std::array<std::byte, 6> decrypted{};
    std::span<const std::byte> ext = ee;
    if (m_ExtensionEncrypted && !m_MotionPlusActive) {
        std::ranges::copy(ee, decrypted.begin());
        DecryptExtensionBytes(decrypted);
        ext = decrypted;
    }

    switch (m_Snapshot.extension) {
        case ExtensionType::Nunchuk:
            m_Snapshot.nunchuk = m_MotionPlusActive
                ? Decode::NunchukViaMotionPlus(ee)
                : Decode::Nunchuk(ext);
            break;
        case ExtensionType::ClassicController:
        case ExtensionType::ClassicControllerPro: {
            const bool is_pro = m_Snapshot.extension == ExtensionType::ClassicControllerPro;
            m_Snapshot.classic = m_MotionPlusActive
                ? Decode::ClassicViaMotionPlus(ee, is_pro)
                : Decode::Classic(ext, is_pro);
            break;
        }
        case ExtensionType::GuitarHeroGuitar:
        case ExtensionType::GuitarHeroDrums: {
            const bool is_drums = m_Snapshot.extension == ExtensionType::GuitarHeroDrums;
            m_Snapshot.guitar = m_MotionPlusActive
                ? Decode::GuitarFromClassic(Decode::ClassicViaMotionPlus(ee, /*is_pro=*/false), is_drums)
                : Decode::Guitar(ext, is_drums);
            break;
        }
        default: break;
    }
}

void WiimoteDevice::DecodeCoreAccelIR12(std::span<const std::byte> report) {
    // (a1) 33 BB BB AA AA AA II II II II II II II II II II II II
    //       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
    // No extension bytes in this report at all - see SetIRMode()'s
    // comment for why. nunchuk/classic/guitar are deliberately left
    // untouched here (not zeroed) so they hold their last known values;
    // ir_extended_mode tells callers those values are frozen, not live.
    // Caller (HandleReport) has already checked report.size() >= 18.
    const auto bb = report.subspan<1, 2>();
    const auto aa = report.subspan<3, 3>();
    const auto ir = report.subspan<6, 12>();

    m_Snapshot.core  = Decode::Buttons(bb);
    m_Snapshot.accel = Decode::Accel(bb, aa);
    m_Snapshot.ir    = Decode::IRExtended(ir);
    m_LastIRReportMs = SDL_GetTicks(); // fed to TickIRWatchdog() - see its comment
    m_Snapshot.ir_possibly_hijacked = false; // this report proves our mode is still in effect right now
}

void WiimoteDevice::DecodeInterleavedIR(std::span<const std::byte> report) {
    // (a1) 3e BB BB AA II II II II II II II II II II II II II II II II II II
    // (a1) 3f BB BB AA II II II II II II II II II II II II II II II II II II
    //       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22
    // Full IR mode. No extension bytes here either, same rationale as
    // DecodeCoreAccelIR12() - nunchuk/classic/guitar stay frozen while
    // this mode is active (ir_extended_mode covers Full too, see its
    // header comment).
    //
    // Two objects (9 bytes each = 18 II bytes) arrive per report, half the
    // set at a time; 0x3e and 0x3f together carry all 4. The accelerometer
    // is also split differently here than every other mode (WiiBrew
    // "Interleaved Accelerometer Reporting"): a single AA byte per report
    // for X (0x3e) / Y (0x3f), with Z spread across 2 bits in each
    // report's BB BB buttons bytes. We don't have a dedicated interleaved-
    // accel decoder/struct yet, so core buttons are still decoded (they're
    // valid every report) but accel is deliberately left at its last
    // value here rather than half-updated from a mismatched normal-mode
    // decoder, which would corrupt it.
    // Caller (HandleReport) has already checked report.size() >= 22.
    const auto bb = report.subspan<1, 2>();
    const bool is_first_half = (report[0] == InReport::InterleavedA);
    const auto ir = report.subspan<4, 18>(); // two 9-byte objects

    m_Snapshot.core = Decode::Buttons(bb);

    if (is_first_half) {
        // Start (or restart) the pair. A dropped/duplicate 0x3e simply
        // means we overwrite whatever was pending - the old half was
        // already incomplete and unusable on its own.
        m_PendingFullDots[0] = Decode::IRFullDot(ir.subspan<0, 9>());
        m_PendingFullDots[1] = Decode::IRFullDot(ir.subspan<9, 9>());
        m_HavePendingFullDots = true;
        return; // wait for the matching 0x3f before publishing a full IRState
    }

    // This is the second half (0x3f). Only publish if it's actually
    // completing a pair we started - a stray 0x3f with no preceding 0x3e
    // (e.g. right after a mode switch, or one report lost on the link)
    // would otherwise pair fresh dots 2-3 with a stale/zeroed dots 0-1.
    if (!m_HavePendingFullDots) return;

    m_Snapshot.ir[0] = m_PendingFullDots[0];
    m_Snapshot.ir[1] = m_PendingFullDots[1];
    m_Snapshot.ir[2] = Decode::IRFullDot(ir.subspan<0, 9>());
    m_Snapshot.ir[3] = Decode::IRFullDot(ir.subspan<9, 9>());
    m_HavePendingFullDots = false;

    m_LastIRReportMs = SDL_GetTicks(); // fed to TickIRWatchdog() - see its comment
    m_Snapshot.ir_possibly_hijacked = false; // this report proves our mode is still in effect right now
}

void WiimoteDevice::DecodeCoreExt19(std::span<const std::byte> report) {
    // (a1) 34 BB BB EE(x19)  - Balance Board steady-state mode. First 11 of
    // the 19 extension bytes are the weight sensors + temperature + battery
    // (see WiiBrew Wii_Balance_Board#Data_Format); the rest are padding.
    // Caller (HandleReport) has already checked report.size() >= 22.
    const auto bb = report.subspan<1, 2>();
    const auto ee = report.subspan<3, 11>();

    m_Snapshot.core = Decode::Buttons(bb);

    BalanceBoardState raw = Decode::BalanceBoard(ee, m_BalanceCal.value_or(BalanceBoardCalibration{}));

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

bool WiimoteDevice::SetIRMode(IRCameraMode mode) {
    if (m_Snapshot.is_balance_board) return false; // no camera hardware
    if (mode == m_IRMode) return true;              // already there

    m_IRMode = mode;

    // Re-run just the mode-select portion of EnableIRCameraOnce()'s WiiBrew
    // sequence (toggle -> mode write -> toggle) rather than all 7 steps -
    // the sensitivity blocks aren't mode-dependent, only the camera's data
    // format is changing. Same >=50ms inter-write delay (kIRInitStepDelayMs)
    // as the rest of that sequence and for the same reason: WiiBrew warns
    // writing these registers back-to-back without a gap can land the
    // camera in a random half-configured state.
    bool ok = true;
    constexpr std::array toggle08{0x08_b};
    ok &= WriteRegister(Registers::IRModeToggle, toggle08);
    SDL_Delay(kIRInitStepDelayMs);
    const std::array reg_mode{
        mode == IRCameraMode::Full ? IRMode::Full :
        mode == IRCameraMode::Extended ? IRMode::Extended : IRMode::Basic};
    ok &= WriteRegister(Registers::IRMode, reg_mode);
    SDL_Delay(kIRInitStepDelayMs);
    ok &= WriteRegister(Registers::IRModeToggle, toggle08);
    SDL_Delay(kIRInitStepDelayMs);

    // Re-assert the data reporting mode so the report ID itself switches
    // (0x37 <-> 0x33 <-> 0x3e) - per WiiBrew this is required after any
    // data format change, mirroring what HandleStatusReport()/
    // TickIRWatchdog() already do for other report-mode transitions.
    const std::array p{0x04_b, PreferredReportMode()};
    ok &= SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p);

    if (!ok) {
        LOG_WARN(kTag, "SetIRMode(%d) had a write failure for %s - "
                        "mode may not have taken effect",
                 static_cast<int>(mode), m_Path.c_str());
    }

    // Give TickIRWatchdog() a clean slate through the transition, same as
    // a fresh EnableIRCamera() success does - we're switching which
    // report ID(s) carry IR data, and don't want a few transitional
    // milliseconds of silence on the old one misread as a hijack.
    m_LastIRReportMs = 0;
    m_Snapshot.ir_possibly_hijacked = false;
    m_IRReassertAttempts = 0;
    m_Snapshot.ir_camera_mode = mode;
    m_Snapshot.ir_extended_mode = (mode != IRCameraMode::Basic);

    // Full mode's dot pairing (see DecodeInterleavedIR()) is meaningless
    // across a mode switch - drop any half-received pair from before the
    // switch so it can't get merged with fresh post-switch data.
    m_HavePendingFullDots = false;

    static const char *kModeNames[] = {"Basic", "Extended", "Full"};
    LOG_INFO(kTag, "IR camera mode set to %s for %s%s", kModeNames[static_cast<int>(mode)],
             m_Path.c_str(),
             (mode != IRCameraMode::Basic) ? " - Nunchuk/Classic/Guitar data is frozen while this is active" : "");

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

    const std::array p{0x04_b, PreferredReportMode()};
    SendReport(m_Transport.get(), OutReport::DataReportMode, m_RumbleBit, p);
}

} // namespace InputBridge::Wiimote
