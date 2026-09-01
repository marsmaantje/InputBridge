#include "App/Log.h"
#include "WindowsWiimoteBluetoothPairing.h"

#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>

static constexpr const char *kTag = "WindowsWiimoteBluetoothPairing";
static constexpr int kDiscoverySeconds = 20; // matches the Wiimote's own sync window

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Devices::Enumeration;

namespace InputBridge::Bluetooth {

// The well-known "Bluetooth" Association Endpoint protocol id used by every
// Microsoft sample that enumerates classic (BR/EDR) Bluetooth radios via
// DeviceInformation - see Microsoft's "DeviceEnumerationAndPairing" and
// "Bluetooth Rfcomm Chat" samples, which both filter on this exact GUID.
static const wchar_t kBluetoothAqsFilter[] =
    L"System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\"";

struct WindowsWiimoteBluetoothPairing::Impl {
    std::mutex mutex;
    bool com_initialized = false;
    bool available = false;

    DeviceWatcher watcher{nullptr};
    winrt::event_token added_token{};
    winrt::event_token updated_token{};
    winrt::event_token removed_token{};
    winrt::event_token stopped_token{};

    bool discovering = false;
    std::chrono::steady_clock::time_point discovery_deadline{};
    WiimotePairing::DeviceFoundCallback on_device;
    WiimotePairing::DiscoveryDoneCallback on_discovery_done;

    // AEP id -> last-known DeviceInformation, so PairDevice() doesn't need
    // a fresh CreateFromIdAsync() round trip and so we can tell whether a
    // device we're being asked to pair is already paired.
    std::map<std::string, DeviceInformation> known_devices;

    std::atomic<bool> stop_requested{false};
    std::thread housekeeping_thread; // watches discovery_deadline; see ctor

    std::string pairing_id; // AEP id currently being paired, if any
    WiimotePairing::PairCallback on_pair_done;

    struct QueuedEvent {
        enum class Kind { DeviceFound, DiscoveryDone, PairDone } kind;
        WiimotePairing::DeviceFoundCallback device_cb;
        DiscoveredDevice device;
        WiimotePairing::DiscoveryDoneCallback discovery_done_cb;
        bool timed_out = false;
        WiimotePairing::PairCallback pair_cb;
        PairResult pair_result = PairResult::Error;
        std::string detail;
    };
    std::deque<QueuedEvent> event_queue;

    static std::string ToUtf8(winrt::hstring const &s) {
        return winrt::to_string(s);
    }

    DiscoveredDevice ToDiscoveredDevice(DeviceInformation const &info) {
        DiscoveredDevice dev;
        dev.address = ToUtf8(info.Id());
        dev.name = ToUtf8(info.Name());
        dev.already_paired = info.Pairing() && info.Pairing().IsPaired();
        dev.looks_like_wiimote = IsWiimoteBluetoothName(dev.name);
        return dev;
    }

    void QueueDeviceFound(DeviceInformation const &info) {
        std::lock_guard<std::mutex> lock(mutex);
        auto dev = ToDiscoveredDevice(info);
        auto id = dev.address;
        known_devices.insert_or_assign(id, info);
        if (!discovering) return;
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::DeviceFound;
        ev.device_cb = on_device;
        ev.device = dev;
        event_queue.push_back(std::move(ev));
    }
};

WindowsWiimoteBluetoothPairing::WindowsWiimoteBluetoothPairing() : m_Impl(std::make_unique<Impl>()) {
    try {
        // Multi-threaded apartment: DeviceWatcher callbacks and our own
        // PairDevice() background thread both need COM initialized on
        // whichever thread touches WinRT objects, and InputBridge's UI
        // thread shouldn't be forced into an STA just for this feature.
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        m_Impl->com_initialized = true;
        m_Impl->available = true;
    } catch (winrt::hresult_error const &ex) {
        LOG_WARN(kTag, "winrt::init_apartment failed: %s", winrt::to_string(ex.message()).c_str());
        m_Impl->available = false;
        return;
    }

    m_Impl->housekeeping_thread = std::thread([impl = m_Impl.get()] {
        while (!impl->stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (impl->discovering && std::chrono::steady_clock::now() >= impl->discovery_deadline) {
                impl->discovering = false;
                try {
                    if (impl->watcher && impl->watcher.Status() == DeviceWatcherStatus::Started) {
                        impl->watcher.Stop();
                    }
                } catch (...) {
                    // Best-effort - Stop() throwing here shouldn't stop us
                    // from reporting the scan as finished.
                }
                Impl::QueuedEvent ev;
                ev.kind = Impl::QueuedEvent::Kind::DiscoveryDone;
                ev.discovery_done_cb = impl->on_discovery_done;
                ev.timed_out = true;
                impl->event_queue.push_back(std::move(ev));
            }
        }
    });
}

WindowsWiimoteBluetoothPairing::~WindowsWiimoteBluetoothPairing() {
    if (!m_Impl) return;
    m_Impl->stop_requested = true;
    if (m_Impl->housekeeping_thread.joinable()) m_Impl->housekeeping_thread.join();

    try {
        if (m_Impl->watcher) {
            m_Impl->watcher.Added(m_Impl->added_token);
            m_Impl->watcher.Updated(m_Impl->updated_token);
            m_Impl->watcher.Removed(m_Impl->removed_token);
            m_Impl->watcher.Stopped(m_Impl->stopped_token);
            if (m_Impl->watcher.Status() == DeviceWatcherStatus::Started) {
                m_Impl->watcher.Stop();
            }
        }
    } catch (...) {
        // Destructor - nothing useful to do with an exception here.
    }
}

bool WindowsWiimoteBluetoothPairing::IsAvailable() const {
    return m_Impl && m_Impl->available;
}

void WindowsWiimoteBluetoothPairing::StartDiscovery(WiimotePairing::DeviceFoundCallback on_device,
                                                      WiimotePairing::DiscoveryDoneCallback on_done) {
    if (!m_Impl || !m_Impl->available) {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        Impl::QueuedEvent ev;
        ev.kind = Impl::QueuedEvent::Kind::DiscoveryDone;
        ev.discovery_done_cb = std::move(on_done);
        ev.timed_out = false;
        m_Impl->event_queue.push_back(std::move(ev));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        m_Impl->on_device = std::move(on_device);
        m_Impl->on_discovery_done = std::move(on_done);
        m_Impl->known_devices.clear();
        m_Impl->discovering = true;
        m_Impl->discovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kDiscoverySeconds);
    }

    try {
        // additionalProperties: just enough to tell already-paired devices
        // apart from new ones - IsWiimoteBluetoothName() + the returned
        // Name cover everything else DiscoveredDevice needs.
        auto additional_properties = winrt::single_threaded_vector<winrt::hstring>(
            {L"System.Devices.Aep.IsPaired"});

        m_Impl->watcher = DeviceInformation::CreateWatcher(
            winrt::hstring(kBluetoothAqsFilter),
            additional_properties,
            DeviceInformationKind::AssociationEndpoint);

        m_Impl->added_token = m_Impl->watcher.Added(
            [impl = m_Impl.get()](DeviceWatcher const &, DeviceInformation const &info) {
                impl->QueueDeviceFound(info);
            });
        m_Impl->updated_token = m_Impl->watcher.Updated(
            [impl = m_Impl.get()](DeviceWatcher const &watcher, DeviceInformationUpdate const &update) {
                // DeviceInformationUpdate only carries the delta; ask the
                // watcher-independent API for a fresh full record rather
                // than trying to hand-merge partial property updates the
                // way the Linux backend does for BlueZ's PropertiesChanged
                // - CreateFromIdAsync here is a cheap local cache lookup on
                // Windows, not a new over-the-air query.
                try {
                    auto op = DeviceInformation::CreateFromIdAsync(update.Id());
                    op.Completed([impl](IAsyncOperation<DeviceInformation> const &sender, AsyncStatus status) {
                        if (status == AsyncStatus::Completed) {
                            impl->QueueDeviceFound(sender.GetResults());
                        }
                    });
                } catch (...) {
                }
            });
        m_Impl->removed_token = m_Impl->watcher.Removed(
            [](DeviceWatcher const &, DeviceInformationUpdate const &) {
                // A device dropping out of range mid-scan doesn't need to
                // update the UI list - it just won't be pairable if chosen,
                // which PairDevice()'s NotFound path already covers.
            });

        m_Impl->watcher.Start();
    } catch (winrt::hresult_error const &ex) {
        LOG_WARN(kTag, "DeviceWatcher setup failed: %s", winrt::to_string(ex.message()).c_str());
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        m_Impl->discovering = false;
        Impl::QueuedEvent ev;
        ev.kind = Impl::QueuedEvent::Kind::DiscoveryDone;
        ev.discovery_done_cb = m_Impl->on_discovery_done;
        ev.timed_out = false;
        m_Impl->event_queue.push_back(std::move(ev));
    }
}

void WindowsWiimoteBluetoothPairing::StopDiscovery() {
    if (!m_Impl) return;
    WiimotePairing::DiscoveryDoneCallback cb;
    bool was_discovering = false;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        was_discovering = m_Impl->discovering;
        m_Impl->discovering = false;
        cb = m_Impl->on_discovery_done;
    }
    if (!was_discovering) return;

    try {
        if (m_Impl->watcher && m_Impl->watcher.Status() == DeviceWatcherStatus::Started) {
            m_Impl->watcher.Stop();
        }
    } catch (...) {
    }

    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    Impl::QueuedEvent ev;
    ev.kind = Impl::QueuedEvent::Kind::DiscoveryDone;
    ev.discovery_done_cb = cb;
    ev.timed_out = false;
    m_Impl->event_queue.push_back(std::move(ev));
}

bool WindowsWiimoteBluetoothPairing::IsDiscovering() const {
    if (!m_Impl) return false;
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->discovering;
}

void WindowsWiimoteBluetoothPairing::PairDevice(const std::string &address, WiimotePairing::PairCallback on_done) {
    if (!m_Impl || !m_Impl->available) {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        Impl::QueuedEvent ev;
        ev.kind = Impl::QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::NotAvailable;
        ev.detail = "Bluetooth is not available.";
        m_Impl->event_queue.push_back(std::move(ev));
        return;
    }

    DeviceInformation info{nullptr};
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        if (!m_Impl->pairing_id.empty()) {
            Impl::QueuedEvent ev;
            ev.kind = Impl::QueuedEvent::Kind::PairDone;
            ev.pair_cb = std::move(on_done);
            ev.pair_result = PairResult::Error;
            ev.detail = "Another pairing attempt is already in progress.";
            m_Impl->event_queue.push_back(std::move(ev));
            return;
        }
        auto it = m_Impl->known_devices.find(address);
        if (it == m_Impl->known_devices.end()) {
            Impl::QueuedEvent ev;
            ev.kind = Impl::QueuedEvent::Kind::PairDone;
            ev.pair_cb = std::move(on_done);
            ev.pair_result = PairResult::NotFound;
            ev.detail = "Device is no longer known - try scanning again.";
            m_Impl->event_queue.push_back(std::move(ev));
            return;
        }
        info = it->second;
        if (info.Pairing() && info.Pairing().IsPaired()) {
            Impl::QueuedEvent ev;
            ev.kind = Impl::QueuedEvent::Kind::PairDone;
            ev.pair_cb = std::move(on_done);
            ev.pair_result = PairResult::AlreadyPaired;
            m_Impl->event_queue.push_back(std::move(ev));
            return;
        }
        m_Impl->pairing_id = address;
        m_Impl->on_pair_done = std::move(on_done);
    }

    // Runs the actual (blocking, coroutine-based-but-.get()'d) pairing
    // exchange on its own thread so PairDevice() itself returns
    // immediately, keeping the "always deliver via Pump()" contract - see
    // WiimoteBluetoothPairing.h.
    std::thread([impl = m_Impl.get(), info]() mutable {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        PairResult result = PairResult::Error;
        std::string detail;

        try {
            auto pairing = info.Pairing();
            auto custom = pairing.Custom();

            auto requested_token = custom.PairingRequested(
                [](DeviceInformationCustomPairing const &, DevicePairingRequestedEventArgs const &args) {
                    // A Wiimote never has a display or keyboard, so every
                    // pairing kind Windows might offer here should be
                    // answered by simply accepting - see
                    // WiimoteBluetoothPairing.h's Just Works note. For the
                    // legacy PIN-code kinds (ProvidePin/DisplayPin),
                    // Windows' own Bluetooth stack already knows how to
                    // negotiate the HID-device pairing convention
                    // internally; we don't need to (and via this API,
                    // can't easily) supply a PIN value ourselves.
                    args.Accept();
                });

            auto kinds = DevicePairingKinds::ConfirmOnly |
                         DevicePairingKinds::ProvidePin |
                         DevicePairingKinds::DisplayPin;
            auto pairing_result = custom.PairAsync(kinds, DevicePairingProtectionLevel::Default).get();

            custom.PairingRequested(requested_token);

            switch (pairing_result.Status()) {
                case DevicePairingResultStatus::Paired:
                    result = PairResult::Success;
                    break;
                case DevicePairingResultStatus::AlreadyPaired:
                    result = PairResult::AlreadyPaired;
                    break;
                case DevicePairingResultStatus::PairingCanceled:
                case DevicePairingResultStatus::RejectedByHandler:
                case DevicePairingResultStatus::AuthenticationFailure:
                    result = PairResult::Rejected;
                    break;
                case DevicePairingResultStatus::OperationAlreadyInProgress:
                case DevicePairingResultStatus::ConnectionRejected:
                    result = PairResult::Rejected;
                    break;
                default:
                    result = PairResult::Error;
                    detail = "Pairing failed (status " +
                              std::to_string(static_cast<int>(pairing_result.Status())) + ").";
                    break;
            }
        } catch (winrt::hresult_error const &ex) {
            result = PairResult::Error;
            detail = winrt::to_string(ex.message());
        }

        std::lock_guard<std::mutex> lock(impl->mutex);
        Impl::QueuedEvent ev;
        ev.kind = Impl::QueuedEvent::Kind::PairDone;
        ev.pair_cb = impl->on_pair_done;
        ev.pair_result = result;
        ev.detail = detail;
        impl->event_queue.push_back(std::move(ev));
        impl->on_pair_done = nullptr;
        impl->pairing_id.clear();
    }).detach();
}

void WindowsWiimoteBluetoothPairing::Pump() {
    if (!m_Impl) return;
    std::deque<Impl::QueuedEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        events.swap(m_Impl->event_queue);
    }
    for (auto &ev : events) {
        switch (ev.kind) {
            case Impl::QueuedEvent::Kind::DeviceFound:
                if (ev.device_cb) ev.device_cb(ev.device);
                break;
            case Impl::QueuedEvent::Kind::DiscoveryDone:
                if (ev.discovery_done_cb) ev.discovery_done_cb(ev.timed_out);
                break;
            case Impl::QueuedEvent::Kind::PairDone:
                if (ev.pair_cb) ev.pair_cb(ev.pair_result, ev.detail);
                break;
        }
    }
}

} // namespace InputBridge::Bluetooth

#endif // _WIN32
