// src/Devices/Wiimote/WiimoteHidTransport.cpp
#include "WiimoteHidTransport.h"

namespace InputBridge::Wiimote {

WiimoteHidTransport::WiimoteHidTransport(SDL_hid_device *dev) : m_Dev(dev) {
    if (m_Dev) SDL_hid_set_nonblocking(m_Dev, 1);
}

WiimoteHidTransport::~WiimoteHidTransport() { Close(); }

namespace {
// SDL_hid_* takes `unsigned char *`. Viewing a std::byte buffer through
// unsigned char is well-defined (both are exempt from strict aliasing and
// have identical size/alignment), and this is the single place the module
// needs to do it.
const unsigned char *AsUChar(std::span<const std::byte> data) {
    return reinterpret_cast<const unsigned char *>(data.data()); // NOSONAR: SDL_hid API boundary
}
unsigned char *AsUChar(std::span<std::byte> data) {
    return reinterpret_cast<unsigned char *>(data.data()); // NOSONAR: SDL_hid API boundary
}
} // namespace

int WiimoteHidTransport::Write(std::span<const std::byte> data) {
    if (!m_Dev) return -1;
    return static_cast<int>(SDL_hid_write(m_Dev, AsUChar(data), data.size()));
}

int WiimoteHidTransport::Read(std::span<std::byte> buf) {
    if (!m_Dev) return -1;
    return static_cast<int>(SDL_hid_read(m_Dev, AsUChar(buf), buf.size()));
}

void WiimoteHidTransport::Close() {
    if (m_Dev) {
        SDL_hid_close(m_Dev);
        m_Dev = nullptr;
    }
}

} // namespace InputBridge::Wiimote
