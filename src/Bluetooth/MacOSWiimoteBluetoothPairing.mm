#import "App/Log.h"
#import "MacOSWiimoteBluetoothPairing.h"

#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <IOBluetooth/IOBluetooth.h>
#import <dispatch/dispatch.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

static constexpr const char *kTag = "MacOSWiimoteBluetoothPairing";
static constexpr NSTimeInterval kDiscoverySeconds = 20.0; // matches the Wiimote's own sync window

namespace InputBridge::Bluetooth {

struct MacOSWiimoteBluetoothPairing::Impl {
    std::mutex mutex;
    bool available = false;

    bool discovering = false;
    std::chrono::steady_clock::time_point discovery_deadline{};
    WiimotePairing::DeviceFoundCallback on_device;
    WiimotePairing::DiscoveryDoneCallback on_discovery_done;

    // address string -> device, so PairDevice() doesn't need a second
    // lookup and so we can tell it "already paired" without a fresh query.
    std::map<std::string, IOBluetoothDevice *> known_devices;

    std::string pairing_address;
    WiimotePairing::PairCallback on_pair_done;

    IOBluetoothDeviceInquiry *inquiry = nil;
    id inquiry_delegate = nil;  // InputBridgeWiimoteInquiryDelegate*
    id connect_delegate = nil;  // InputBridgeWiimoteConnectDelegate*, kept
                                 // alive (via this strong ref) for the
                                 // duration of one outstanding openConnection

    std::atomic<bool> stop_requested{false};
    std::thread housekeeping_thread; // watches discovery_deadline; see ctor

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

    static DiscoveredDevice ToDiscoveredDevice(IOBluetoothDevice *device) {
        DiscoveredDevice dev;
        NSString *addr = [device addressString];
        dev.address = addr ? std::string([addr UTF8String]) : "";
        NSString *name = [device name];
        dev.name = name ? std::string([name UTF8String]) : "";
        dev.already_paired = [device isPaired];
        dev.looks_like_wiimote = IsWiimoteBluetoothName(dev.name);
        return dev;
    }

    void OnDeviceFound(IOBluetoothDevice *device) {
        std::lock_guard<std::mutex> lock(mutex);
        DiscoveredDevice dev = ToDiscoveredDevice(device);
        if (dev.address.empty()) return;
        known_devices[dev.address] = device;
        if (!discovering) return;
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::DeviceFound;
        ev.device_cb = on_device;
        ev.device = dev;
        event_queue.push_back(std::move(ev));
    }

    void OnInquiryComplete(BOOL aborted) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!discovering) return; // already handled by our own deadline/StopDiscovery
        discovering = false;
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::DiscoveryDone;
        ev.discovery_done_cb = on_discovery_done;
        // IOBluetoothDeviceInquiry also has its own built-in inquiry
        // length (we set it to match kDiscoverySeconds below) - either it
        // or our own deadline ends the scan first; treat "not aborted by
        // us" as a normal, expected end of the scan window either way.
        ev.timed_out = !aborted;
        event_queue.push_back(std::move(ev));
    }

    void OnConnectionComplete(IOBluetoothDevice *device, IOReturn status) {
        std::lock_guard<std::mutex> lock(mutex);
        PairResult result;
        std::string detail;
        if (status == kIOReturnSuccess) {
            // openConnection succeeding on a not-yet-paired device means
            // macOS's Bluetooth daemon completed baseband pairing as part
            // of bringing the connection up (see this class's header
            // comment) - there's no separate "pairing succeeded" callback
            // to wait for beyond this.
            result = PairResult::Success;
        } else {
            result = PairResult::Error;
            detail = "IOReturn " + std::to_string(static_cast<int>(status));
        }
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = on_pair_done;
        ev.pair_result = result;
        ev.detail = detail;
        event_queue.push_back(std::move(ev));
        on_pair_done = nullptr;
        pairing_address.clear();
        connect_delegate = nil;
    }
};

} // namespace InputBridge::Bluetooth

// --- Objective-C delegates ----------------------------------------------------
// Kept file-local (not exposed via the header) - see MacOSWiimoteBluetoothPairing.h
// for why the public API stays plain C++.

@interface InputBridgeWiimoteInquiryDelegate : NSObject <IOBluetoothDeviceInquiryDelegate>
@property (nonatomic, assign) InputBridge::Bluetooth::MacOSWiimoteBluetoothPairing::Impl *impl;
@end

@implementation InputBridgeWiimoteInquiryDelegate
- (void)deviceInquiryDeviceFound:(IOBluetoothDeviceInquiry *)sender device:(IOBluetoothDevice *)device {
    (void)sender;
    if (self.impl) self.impl->OnDeviceFound(device);
}
- (void)deviceInquiryDeviceNameUpdated:(IOBluetoothDeviceInquiry *)sender
                                 device:(IOBluetoothDevice *)device
                       devicesRemaining:(uint32_t)devicesRemaining {
    (void)sender;
    (void)devicesRemaining;
    // A device's name often isn't known yet on the first sighting; this
    // fires once it resolves, so we can update looks_like_wiimote/UI text.
    if (self.impl) self.impl->OnDeviceFound(device);
}
- (void)deviceInquiryComplete:(IOBluetoothDeviceInquiry *)sender error:(IOReturn)error aborted:(BOOL)aborted {
    (void)sender;
    (void)error;
    if (self.impl) self.impl->OnInquiryComplete(aborted);
}
@end

@interface InputBridgeWiimoteConnectDelegate : NSObject
@property (nonatomic, assign) InputBridge::Bluetooth::MacOSWiimoteBluetoothPairing::Impl *impl;
@end

@implementation InputBridgeWiimoteConnectDelegate
// Informal IOBluetoothDevice connection-complete callback - see
// -[IOBluetoothDevice openConnection:withPageTimeout:authenticationRequired:]
// in IOBluetoothDevice.h ("target" must implement this selector).
- (void)connectionComplete:(IOBluetoothDevice *)device status:(IOReturn)status {
    if (self.impl) self.impl->OnConnectionComplete(device, status);
}
@end

namespace InputBridge::Bluetooth {

MacOSWiimoteBluetoothPairing::MacOSWiimoteBluetoothPairing() : m_Impl(std::make_unique<Impl>()) {
    // There's no cheap synchronous "is a Bluetooth controller present"
    // check exposed the way BlueZ's D-Bus ObjectManager gives the Linux
    // backend one - IOBluetooth is available whenever the framework loads,
    // and a real absence of hardware surfaces later as a StartDiscovery()/
    // PairDevice() failure instead.
    m_Impl->available = true;

    // Every actual IOBluetooth call in this file is dispatched onto the
    // main queue (see the calls below) rather than issued from whatever
    // thread the public API happens to be called on - IOBluetooth
    // delivers its own delegate callbacks via the run loop of the thread
    // that started the operation, and the main thread reliably has one
    // pumping in an SDL app; a background thread's run loop is not
    // guaranteed to be serviced. This housekeeping thread therefore only
    // ever *requests* a stop via dispatch_async - it never touches
    // IOBluetooth objects directly.
    m_Impl->housekeeping_thread = std::thread([impl = m_Impl.get()] {
        while (!impl->stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (impl->discovering && std::chrono::steady_clock::now() >= impl->discovery_deadline) {
                impl->discovering = false;
                IOBluetoothDeviceInquiry *inquiry = impl->inquiry;
                if (inquiry) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [inquiry stop];
                    });
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

MacOSWiimoteBluetoothPairing::~MacOSWiimoteBluetoothPairing() {
    if (!m_Impl) return;
    m_Impl->stop_requested = true;
    if (m_Impl->housekeeping_thread.joinable()) m_Impl->housekeeping_thread.join();

    IOBluetoothDeviceInquiry *inquiry = m_Impl->inquiry;
    if (inquiry) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [inquiry stop];
            [inquiry setDelegate:nil];
        });
    }
    m_Impl->inquiry = nil;
    m_Impl->inquiry_delegate = nil;
    m_Impl->connect_delegate = nil;
}

bool MacOSWiimoteBluetoothPairing::IsAvailable() const {
    return m_Impl && m_Impl->available;
}

void MacOSWiimoteBluetoothPairing::StartDiscovery(WiimotePairing::DeviceFoundCallback on_device,
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
        m_Impl->discovery_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(kDiscoverySeconds));
    }

    Impl *impl = m_Impl.get();
    dispatch_async(dispatch_get_main_queue(), ^{
        InputBridgeWiimoteInquiryDelegate *delegate = [[InputBridgeWiimoteInquiryDelegate alloc] init];
        delegate.impl = impl;

        IOBluetoothDeviceInquiry *inquiry = [IOBluetoothDeviceInquiry inquiryWithDelegate:delegate];
        // IOBluetoothDeviceInquiry's -setInquiryLength: takes seconds as
        // an unsigned integer type (IOBluetoothDeviceInquiry.h) - using a
        // plain numeric cast here rather than naming that type directly
        // since it's not worth risking a typo against a header this
        // hasn't been compiled against as part of this change.
        [inquiry setInquiryLength:(unsigned)kDiscoverySeconds];
        [inquiry setUpdateNewDeviceNames:YES];

        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->inquiry = inquiry;
            impl->inquiry_delegate = delegate;
        }

        // Surface anything already known/paired immediately, same as the
        // other two backends, rather than waiting on a fresh inquiry
        // result for devices macOS has already seen.
        for (IOBluetoothDevice *paired in [IOBluetoothDevice pairedDevices]) {
            impl->OnDeviceFound(paired);
        }

        IOReturn err = [inquiry start];
        if (err != kIOReturnSuccess) {
            LOG_WARN(kTag, "IOBluetoothDeviceInquiry start failed: IOReturn %d", err);
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->discovering = false;
            Impl::QueuedEvent ev;
            ev.kind = Impl::QueuedEvent::Kind::DiscoveryDone;
            ev.discovery_done_cb = impl->on_discovery_done;
            ev.timed_out = false;
            impl->event_queue.push_back(std::move(ev));
        }
    });
}

void MacOSWiimoteBluetoothPairing::StopDiscovery() {
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

    IOBluetoothDeviceInquiry *inquiry = m_Impl->inquiry;
    if (inquiry) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [inquiry stop];
        });
    }

    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    Impl::QueuedEvent ev;
    ev.kind = Impl::QueuedEvent::Kind::DiscoveryDone;
    ev.discovery_done_cb = cb;
    ev.timed_out = false;
    m_Impl->event_queue.push_back(std::move(ev));
}

bool MacOSWiimoteBluetoothPairing::IsDiscovering() const {
    if (!m_Impl) return false;
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    return m_Impl->discovering;
}

void MacOSWiimoteBluetoothPairing::PairDevice(const std::string &address, WiimotePairing::PairCallback on_done) {
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

    IOBluetoothDevice *device = nil;
    {
        std::lock_guard<std::mutex> lock(m_Impl->mutex);
        if (!m_Impl->pairing_address.empty()) {
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
        device = it->second;
        if ([device isPaired]) {
            Impl::QueuedEvent ev;
            ev.kind = Impl::QueuedEvent::Kind::PairDone;
            ev.pair_cb = std::move(on_done);
            ev.pair_result = PairResult::AlreadyPaired;
            m_Impl->event_queue.push_back(std::move(ev));
            return;
        }
        m_Impl->pairing_address = address;
        m_Impl->on_pair_done = std::move(on_done);
    }

    Impl *impl = m_Impl.get();
    dispatch_async(dispatch_get_main_queue(), ^{
        InputBridgeWiimoteConnectDelegate *delegate = [[InputBridgeWiimoteConnectDelegate alloc] init];
        delegate.impl = impl;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->connect_delegate = delegate; // keep it alive for the connection's duration
        }

        // authenticationRequired:YES asks the Bluetooth daemon to pair
        // (not just connect) if the device isn't paired yet - this is what
        // actually drives the Just Works handshake for a fresh Wiimote.
        // Page timeout: 0x2000 (8192 * 0.625ms ~= 5.12s), the Bluetooth
        // Core Spec's own default HCI page timeout - used as a literal
        // here rather than a named IOBluetooth constant since this hasn't
        // been compiled against a real SDK as part of this change and a
        // slightly-wrong constant name would silently fail to build.
        IOReturn err = [device openConnection:delegate
                              withPageTimeout:0x2000
                          authenticationRequired:YES];
        if (err != kIOReturnSuccess) {
            // openConnection: is documented as either invoking
            // connectionComplete:status: asynchronously, or - for certain
            // immediate failures - returning a non-success IOReturn
            // without ever calling back. Handle the latter here so a
            // pairing attempt can't hang forever waiting for a callback
            // that will never arrive.
            impl->OnConnectionComplete(device, err);
        }
    });
}

void MacOSWiimoteBluetoothPairing::Pump() {
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

#endif // __APPLE__
