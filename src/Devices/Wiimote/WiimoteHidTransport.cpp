// src/Devices/Wiimote/WiimoteHidTransport.cpp
#include "WiimoteHidTransport.h"

namespace InputBridge::Wiimote {

WiimoteHidTransport::WiimoteHidTransport(SDL_hid_device *dev) : m_Dev(dev) {
    if (m_Dev) SDL_hid_set_nonblocking(m_Dev, 1);
}

WiimoteHidTransport::~WiimoteHidTransport() { Close(); }

int WiimoteHidTransport::Write(const uint8_t *data, size_t len) {
    if (!m_Dev) return -1;
    return int(SDL_hid_write(m_Dev, data, len));
}

int WiimoteHidTransport::Read(uint8_t *buf, size_t bufsize) {
    if (!m_Dev) return -1;
    return int(SDL_hid_read(m_Dev, buf, bufsize));
}

void WiimoteHidTransport::Close() {
    if (m_Dev) {
        SDL_hid_close(m_Dev);
        m_Dev = nullptr;
    }
}

} // namespace InputBridge::Wiimote
