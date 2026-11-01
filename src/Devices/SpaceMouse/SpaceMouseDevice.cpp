// src/Devices/SpaceMouse/SpaceMouseDevice.cpp
#include "SpaceMouseDevice.h"
#include "App/Log.h"
#include <SDL3/SDL_timer.h>
#include <array>
#include <utility>

namespace InputBridge::SpaceMouse {

namespace {
constexpr const char *kTag = "SpaceMouseDevice";
// A busy device reports at ~60-125 Hz per report type; this bound only
// guards against a pathological backlog stalling the UI thread.
constexpr int kMaxReportsPerPoll = 64;
constexpr size_t kReadBufSize = 64;
} // namespace

SpaceMouseDevice::SpaceMouseDevice(SDL_hid_device *dev, std::string path, std::string serial,
                                   const ModelInfo &model)
    : m_Dev(dev) {
    m_Snap.hid_path = std::move(path);
    m_Snap.serial   = std::move(serial);
    m_Snap.model    = model;
    if (m_Dev) SDL_hid_set_nonblocking(m_Dev, 1);
}

SpaceMouseDevice::~SpaceMouseDevice() { Close(); }

void SpaceMouseDevice::Close() {
    if (m_Dev) {
        SDL_hid_close(m_Dev);
        m_Dev = nullptr;
    }
}

// A vanished device sends no "released" report, so without this the bridge
// joystick would keep the last deflection / pressed buttons until it is pruned.
void SpaceMouseDevice::ReleaseInputs() {
    m_Snap.state.raw.fill(0);
    m_Snap.state.buttons = 0;
}

void SpaceMouseDevice::Poll() {
    if (!m_Dev) {
        m_Snap.connected = false;
        ReleaseInputs();
        return;
    }
    std::array<uint8_t, kReadBufSize> buf{};
    for (int i = 0; i < kMaxReportsPerPoll; ++i) {
        const int n = SDL_hid_read(m_Dev, buf.data(), static_cast<int>(buf.size()));
        if (n < 0) {
            LOG_INFO(kTag, "Read failed on '%s' - treating device as disconnected",
                     m_Snap.hid_path.c_str());
            m_Snap.connected = false;
            ReleaseInputs();
            Close();
            return;
        }
        if (n == 0) return; // nothing pending
        if (DecodeReport(buf.data(), static_cast<size_t>(n), m_Snap.state) != ReportKind::Ignored) {
            m_Snap.last_report_ms = SDL_GetTicks();
        }
    }
}

} // namespace InputBridge::SpaceMouse
