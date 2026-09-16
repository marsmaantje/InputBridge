// src/Devices/Wiimote/Linux/WiimoteL2CAPTransport.cpp
#ifdef __linux__
#include "WiimoteL2CAPTransport.h"
#include "App/Log.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <endian.h>

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "WiimoteL2CAP";

// -- Minimal AF_BLUETOOTH/L2CAP ABI, self-contained (see header comment
// for why this isn't just #include <bluetooth/bluetooth.h>) -------------

constexpr int kBtProtoL2CAP = 0; // BTPROTO_L2CAP, per the kernel's bluetooth.h enum

// Bluetooth device address, stored least-significant-byte-first on the
// wire/in this struct - the reverse of the human-readable "AA:BB:CC:DD:
// EE:FF" order. #pragma pack keeps this at exactly 6 bytes, matching the
// kernel's expectation with no compiler-inserted padding.
#pragma pack(push, 1)
struct RawBdaddr {
    uint8_t b[6];
};

struct SockaddrL2 {
    sa_family_t l2_family;
    uint16_t    l2_psm;      // little-endian on the wire (see ToLeBytes below)
    RawBdaddr   l2_bdaddr;
    uint16_t    l2_cid;
    uint8_t     l2_bdaddr_type;
};
#pragma pack(pop)

constexpr uint8_t kBdaddrTypeBrEdr = 0x00;
constexpr uint16_t kPsmControl   = 0x0011;
constexpr uint16_t kPsmInterrupt = 0x0013;

// ParseBluetoothAddress() (WiimoteBluetoothUtil.h) hands back bytes in
// human-reading order (b[0] == the "AA" in "AA:BB:CC:DD:EE:FF"); the
// kernel's sockaddr_l2 wants the reverse of that.
RawBdaddr ToRawBdaddr(const std::array<uint8_t, 6> &human_order) {
    RawBdaddr r{};
    for (int i = 0; i < 6; ++i) r.b[i] = human_order[5 - i];
    return r;
}

// Opens one L2CAP channel to `bdaddr` on `psm`, blocking with a bounded
// timeout (connect latency over Bluetooth classic is radio-bound, so a few
// seconds is reasonable rather than blocking forever on a dead remote).
// Returns -1 on any failure, having already closed the fd - no cleanup
// needed by the caller.
int OpenL2CAPChannel(const std::array<uint8_t, 6> &bdaddr, uint16_t psm) {
    const int fd = ::socket(AF_BLUETOOTH, SOCK_SEQPACKET, kBtProtoL2CAP);
    if (fd < 0) {
        LOG_WARN(kTag, "socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP) failed: %s", std::strerror(errno));
        return -1;
    }

    // Bind "any" local address with PSM 0 so the kernel picks the local
    // HCI route and auto-assigns a local PSM - same convention BlueZ
    // itself uses for L2CAP client sockets.
    SockaddrL2 local{};
    local.l2_family = AF_BLUETOOTH;
    local.l2_psm = 0;
    local.l2_bdaddr_type = kBdaddrTypeBrEdr;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0) {
        LOG_WARN(kTag, "bind() on local L2CAP socket failed: %s", std::strerror(errno));
        ::close(fd);
        return -1;
    }

    // Non-blocking connect + poll-with-timeout so a silent Wiimote can't
    // hang WiimoteManager::Scan() (called from the main thread).
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    SockaddrL2 remote{};
    remote.l2_family = AF_BLUETOOTH;
    remote.l2_psm = htole16(psm);
    remote.l2_bdaddr = ToRawBdaddr(bdaddr);
    remote.l2_bdaddr_type = kBdaddrTypeBrEdr;

    const int rc = ::connect(fd, reinterpret_cast<sockaddr *>(&remote), sizeof(remote));
    if (rc < 0 && errno != EINPROGRESS) {
        LOG_WARN(kTag, "connect() to PSM 0x%02x failed: %s (device may already be connected "
                        "to the OS's own Bluetooth HID service - see "
                        "WiimoteBluetoothUtil::DisconnectExistingHidConnection())",
                 psm, std::strerror(errno));
        ::close(fd);
        return -1;
    }

    if (rc < 0) { // EINPROGRESS - wait for it to resolve
        constexpr int kConnectTimeoutMs = 5000;
        pollfd pfd{fd, POLLOUT, 0};
        const int poll_rc = ::poll(&pfd, 1, kConnectTimeoutMs);
        if (poll_rc <= 0) {
            LOG_WARN(kTag, "connect() to PSM 0x%02x timed out waiting for the link", psm);
            ::close(fd);
            return -1;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
            LOG_WARN(kTag, "connect() to PSM 0x%02x failed: %s", psm, std::strerror(so_error));
            ::close(fd);
            return -1;
        }
    }

    return fd; // left non-blocking, matching WiimoteHidTransport's contract
}
} // namespace

std::unique_ptr<WiimoteL2CAPTransport> WiimoteL2CAPTransport::Connect(const std::array<uint8_t, 6> &bdaddr) {
    // Control channel first, then interrupt - same order Dolphin and
    // every other open-source Wiimote driver uses; some Wiimote firmware
    // revisions are documented as rejecting the interrupt connection if
    // it arrives before the control one.
    const int control_fd = OpenL2CAPChannel(bdaddr, kPsmControl);
    if (control_fd < 0) return nullptr;

    const int interrupt_fd = OpenL2CAPChannel(bdaddr, kPsmInterrupt);
    if (interrupt_fd < 0) {
        ::close(control_fd);
        return nullptr;
    }

    LOG_INFO(kTag, "Connected direct L2CAP control+interrupt channels (bypassing hidraw)");
    return std::unique_ptr<WiimoteL2CAPTransport>(new WiimoteL2CAPTransport(control_fd, interrupt_fd));
}

WiimoteL2CAPTransport::WiimoteL2CAPTransport(int control_fd, int interrupt_fd)
    : m_ControlFd(control_fd), m_InterruptFd(interrupt_fd) {}

WiimoteL2CAPTransport::~WiimoteL2CAPTransport() { Close(); }

int WiimoteL2CAPTransport::Write(const uint8_t *data, size_t len) {
    if (m_InterruptFd < 0 || len == 0) return -1;
    // WiiBrew Bluetooth HID framing: output reports go out on the
    // interrupt channel prefixed with 0xA2 ("HID DATA, Output report").
    // `data[0]` is already the Wiimote report ID (e.g. 0x13, 0x16) per
    // IWiimoteTransport's contract - it becomes the second byte on the
    // wire, right after the 0xA2 prefix.
    uint8_t framed[64];
    if (len + 1 > sizeof(framed)) return -1; // no real Wiimote report is this large
    framed[0] = 0xA2;
    std::memcpy(framed + 1, data, len);
    const ssize_t n = ::send(m_InterruptFd, framed, len + 1, MSG_NOSIGNAL);
    if (n < 0) return -1;
    return int(n) - 1; // report the same "payload bytes written" count SDL_hid_write would
}

int WiimoteL2CAPTransport::Read(uint8_t *buf, size_t bufsize) {
    if (m_InterruptFd < 0) return -1;
    uint8_t framed[64];
    const ssize_t n = ::recv(m_InterruptFd, framed, sizeof(framed), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // nothing pending right now
        return -1;
    }
    if (n == 0) return -1; // remote closed the connection
    // Strip the 0xA1 ("HID DATA, Input report") prefix so callers see
    // exactly the same report-ID-first bytes WiimoteHidTransport hands
    // them (the kernel's hid-generic driver does this same stripping,
    // invisibly, for the hidraw path).
    if (framed[0] != 0xA1 || n < 2) return 0; // not a data-input frame we understand; ignore
    const size_t payload_len = std::min<size_t>(size_t(n) - 1, bufsize);
    std::memcpy(buf, framed + 1, payload_len);
    return int(payload_len);
}

void WiimoteL2CAPTransport::Close() {
    if (m_InterruptFd >= 0) { ::close(m_InterruptFd); m_InterruptFd = -1; }
    if (m_ControlFd >= 0) { ::close(m_ControlFd); m_ControlFd = -1; }
}

} // namespace InputBridge::Wiimote

#endif // __linux__
