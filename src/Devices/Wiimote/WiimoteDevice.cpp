// src/Devices/Wiimote/WiimoteDevice.cpp
#include "WiimoteDevice.h"
#include "WiimoteDecoder.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>

namespace InputBridge::Wiimote {

namespace {
constexpr int kReportBufSize = 22; // largest fixed report we consume (0x37 = 22 incl. report ID)
constexpr int kRegisterReadTimeoutMs = 250;
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

// ── Output report helpers ───────────────────────────────────────────────

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
    return m_Snapshot.is_balance_board ? InReport::CoreExt19 : InReport::CoreAccelIR10Ext6;
}

bool WiimoteDevice::EnableIRCamera() {
    bool ok = true;
    uint8_t enable[1] = {0x04};
    ok &= SendReport(m_Dev, OutReport::IRCameraEnable1, m_RumbleBit, enable, 1);
    ok &= SendReport(m_Dev, OutReport::IRCameraEnable2, m_RumbleBit, enable, 1);

    // Per WiiBrew: toggle -> sensitivity block 1 -> block 2 -> mode -> toggle
    // again, each as a register write. A ~50ms gap between writes is
    // recommended to avoid landing in an inconsistent camera state; we rely
    // on SDL_hid_write's inherent USB/BT scheduling latency here rather than
    // sleeping the caller's thread explicitly. If dots don't show up
    // reliably in testing, add small delays between these WriteRegister
    // calls or retry Init() once.
    uint8_t toggle08 = 0x08;
    ok &= WriteRegister(Registers::IRModeToggle, &toggle08, 1);
    ok &= WriteRegister(Registers::IRSensitivity1, kIRSensitivityWiiLevel3.block1.data(),
                         uint8_t(kIRSensitivityWiiLevel3.block1.size()));
    ok &= WriteRegister(Registers::IRSensitivity2, kIRSensitivityWiiLevel3.block2.data(),
                         uint8_t(kIRSensitivityWiiLevel3.block2.size()));
    uint8_t mode = IRMode::Basic; // matches report 0x37's 10 IR bytes
    ok &= WriteRegister(Registers::IRMode, &mode, 1);
    ok &= WriteRegister(Registers::IRModeToggle, &toggle08, 1);

    m_Snapshot.ir_enabled = ok;
    return ok;
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
        return false;
    }

    m_Snapshot.extension = ClassifyExtension(id);

    if (m_Snapshot.extension == ExtensionType::BalanceBoard ||
        m_Snapshot.is_balance_board) {
        LoadBalanceBoardCalibration();
    }
    return true;
}

bool WiimoteDevice::LoadBalanceBoardCalibration() {
    uint8_t block[32] = {};
    if (!ReadRegister(Registers::ExtensionCalib, 32, block)) return false;
    m_BalanceCal = Decode::ParseBalanceBoardCalibration(block);
    return true;
}

// ── Feedback ─────────────────────────────────────────────────────────────

void WiimoteDevice::SetPlayerLED(int player_1to4) {
    const uint8_t bit = uint8_t(0x10 << std::clamp(player_1to4 - 1, 0, 3));
    SetLEDMask(bit);
}

void WiimoteDevice::SetLEDMask(uint8_t mask4bits) {
    uint8_t p[1] = {mask4bits};
    SendReport(m_Dev, OutReport::LEDs, m_RumbleBit, p, 1);
    m_Snapshot.led_mask = mask4bits;
}

void WiimoteDevice::SetRumble(bool on) {
    m_RumbleBit = on;
    m_Snapshot.rumble_on = on;
    // Any report re-asserts the rumble bit; a dedicated Rumble (0x10) report
    // with an otherwise-empty payload is the lightest way to do that on demand.
    uint8_t p[1] = {0x00};
    SendReport(m_Dev, OutReport::Rumble, m_RumbleBit, p, 1);
}

// ── Register read/write (synchronous, bounded wait) ─────────────────────

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
            if (n <= 0) continue;
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

// ── Input report handling ───────────────────────────────────────────────

void WiimoteDevice::Poll() {
    if (!m_Dev) return;
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
    m_BalanceCal.reset();
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

void WiimoteDevice::DecodeCoreExt19(const uint8_t *buf) {
    // (a1) 34 BB BB EE(x19)  - Balance Board steady-state mode. First 11 of
    // the 19 extension bytes are the weight sensors + temperature + battery
    // (see WiiBrew Wii_Balance_Board#Data_Format); the rest are padding.
    const uint8_t *bb = buf + 1;
    const uint8_t *ee = buf + 3;

    m_Snapshot.core = Decode::Buttons(bb);

    uint8_t ext11[11];
    std::memcpy(ext11, ee, 11);
    m_Snapshot.balance_board = Decode::BalanceBoard(ext11, m_BalanceCal.value_or(BalanceBoardCalibration{}));
    m_Snapshot.balance_board.button_a = m_Snapshot.core.a;
}

} // namespace InputBridge::Wiimote
