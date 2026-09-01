#include "App/Log.h"
#include "WiimoteBluetoothPairing.h"
#include "WiimoteBluetoothPairingImpl.h"

#ifdef ENABLE_WIIMOTE_BLUETOOTH_PAIRING
#  ifdef _WIN32
#    include "WindowsWiimoteBluetoothPairing.h"
#  elif defined(__linux__)
#    include "LinuxWiimoteBluetoothPairing.h"
#  elif defined(__APPLE__)
#    include "MacOSWiimoteBluetoothPairing.h"
#  endif
#endif

static constexpr const char *kTag = "WiimoteBluetoothPairing";

namespace InputBridge::Bluetooth {

const char *ToString(PairResult result) {
    switch (result) {
        case PairResult::Success:      return "Success";
        case PairResult::AlreadyPaired: return "AlreadyPaired";
        case PairResult::NotFound:     return "NotFound";
        case PairResult::Rejected:     return "Rejected";
        case PairResult::Timeout:      return "Timeout";
        case PairResult::NotAvailable: return "NotAvailable";
        case PairResult::Error:        return "Error";
    }
    return "Unknown";
}

bool IsWiimoteBluetoothName(const std::string &name) {
    // Kept in sync by hand with WiimoteManager::IsWiimoteProductString() -
    // same two substrings, same reasoning (see that function's comment):
    // "Nintendo RVL-CNT-01" covers both the original Wii Remote and the
    // Wii Remote Plus (whose full string has a "-TR" suffix, still matched
    // by the substring search), "Nintendo RVL-WBC-01" is the Balance
    // Board.
    return name.find("RVL-CNT-01") != std::string::npos ||
           name.find("RVL-WBC-01") != std::string::npos;
}

// --- Dummy fallback for platforms/builds with no backend --------------------

class DummyWiimotePairingImpl : public WiimotePairingImpl {
public:
    bool IsAvailable() const override { return false; }
    void StartDiscovery(WiimotePairing::DeviceFoundCallback,
                         WiimotePairing::DiscoveryDoneCallback on_done) override {
        LOG_INFO(kTag, "Wiimote Bluetooth pairing: not implemented on this platform/build.");
        if (on_done) on_done(/*timed_out=*/false);
    }
    void StopDiscovery() override {}
    bool IsDiscovering() const override { return false; }
    void PairDevice(const std::string &, WiimotePairing::PairCallback on_done) override {
        if (on_done) on_done(PairResult::NotAvailable, "Not implemented on this platform/build.");
    }
};

// --- WiimotePairing -----------------------------------------------------

WiimotePairing::WiimotePairing() {
#ifdef ENABLE_WIIMOTE_BLUETOOTH_PAIRING
#  ifdef _WIN32
    m_Impl = std::make_unique<WindowsWiimoteBluetoothPairing>();
#  elif defined(__linux__)
    m_Impl = std::make_unique<LinuxWiimoteBluetoothPairing>();
#  elif defined(__APPLE__)
    m_Impl = std::make_unique<MacOSWiimoteBluetoothPairing>();
#  else
    m_Impl = std::make_unique<DummyWiimotePairingImpl>();
#  endif
#else
    m_Impl = std::make_unique<DummyWiimotePairingImpl>();
#endif
}

WiimotePairing::~WiimotePairing() = default;

bool WiimotePairing::IsAvailable() const {
    return m_Impl && m_Impl->IsAvailable();
}

void WiimotePairing::StartDiscovery(DeviceFoundCallback on_device, DiscoveryDoneCallback on_done) {
    if (!m_Impl) return;
    m_Impl->StartDiscovery(std::move(on_device), std::move(on_done));
}

void WiimotePairing::StopDiscovery() {
    if (m_Impl) m_Impl->StopDiscovery();
}

bool WiimotePairing::IsDiscovering() const {
    return m_Impl && m_Impl->IsDiscovering();
}

void WiimotePairing::PairDevice(const std::string &address, PairCallback on_done) {
    if (!m_Impl) return;
    m_Impl->PairDevice(address, std::move(on_done));
}

void WiimotePairing::Pump() {
    if (m_Impl) m_Impl->Pump();
}

} // namespace InputBridge::Bluetooth
