#pragma once

#ifdef __linux__

#include "WiimoteBluetoothPairingImpl.h"

#include <dbus/dbus.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace InputBridge::Bluetooth {

// Linux backend: talks to BlueZ (bluetoothd) over its D-Bus API using raw
// libdbus-1, the same interfaces `bluetoothctl` itself drives:
//   org.bluez.Adapter1        - StartDiscovery/StopDiscovery, Powered
//   org.bluez.Device1         - Pair/Connect/Trusted, per discovered device
//   org.bluez.AgentManager1   - RegisterAgent/RequestDefaultAgent
//   org.bluez.Agent1          - implemented *by us* at object path
//                               /org/inputbridge/agent, registered with
//                               capability "NoInputNoOutput" to match a
//                               Wiimote's legacy PIN pairing (see
//                               WiimoteBluetoothPairing.h's header comment)
//
// Threading model: opens its own private D-Bus connection and does all
// D-Bus I/O (dbus_connection_read_write_dispatch) on a dedicated background
// thread, since discovery is an open-ended stream of async signals rather
// than a single call/response. That thread only ever pushes finished
// events into a mutex-guarded queue; StartDiscovery()/PairDevice()'s
// caller-supplied callbacks are invoked exclusively from Pump() on
// whatever thread calls it, per the contract in WiimoteBluetoothPairing.h.
//
// Requires bluetoothd (BlueZ) running and reachable on the system bus,
// with the calling user permitted to talk to org.bluez (this is the
// default on every mainstream desktop Linux distro's polkit rules for a
// logged-in session).
class LinuxWiimoteBluetoothPairing : public WiimotePairingImpl {
public:
    LinuxWiimoteBluetoothPairing();
    ~LinuxWiimoteBluetoothPairing() override;

    bool IsAvailable() const override;

    void StartDiscovery(WiimotePairing::DeviceFoundCallback on_device,
                         WiimotePairing::DiscoveryDoneCallback on_done) override;
    void StopDiscovery() override;
    bool IsDiscovering() const override;

    void PairDevice(const std::string &address, WiimotePairing::PairCallback on_done) override;
    void ConnectDevice(const std::string &address, WiimotePairing::PairCallback on_done) override;

    void Pump() override;

private:
    // --- background thread -------------------------------------------------
    void ThreadMain();
    static DBusHandlerResult MessageFilterThunk(DBusConnection *conn, DBusMessage *msg, void *user_data);
    DBusHandlerResult HandleMessage(DBusMessage *msg);

    // Agent1 method calls arrive here (see comment on class re: capability).
    // Confirmation/authorization methods auto-accept; RequestPinCode always
    // declines (can't be answered safely over D-Bus - see this class's .cpp
    // and WiimoteBluetoothPairing.h for why).
    void HandleAgentMethodCall(DBusMessage *msg);

    void HandleInterfacesAdded(DBusMessage *msg);
    void HandlePropertiesChanged(DBusMessage *msg);
    void ProcessManagedObjectsReply(DBusMessage *reply);
    // Reads Address/Name/Alias/Paired/Class off a Device1 property
    // dictionary (however it arrived - GetManagedObjects, InterfacesAdded,
    // or PropertiesChanged all hand us one of these in slightly different
    // wrapper shapes) and queues a device-found event if it's new/changed.
    void HandleDeviceProperties(const std::string &object_path, DBusMessageIter &props_dict_iter);

    bool FindAdapterPath();
    bool EnsureAdapterPowered();
    bool RegisterAgent();

    void FinishPairing(PairResult result, const std::string &detail);
    static void PairReplyThunk(DBusPendingCall *pending, void *user_data);
    void HandlePairReply(DBusPendingCall *pending);
    void ContinueToTrustAndConnect();

    // --- state (all guarded by m_Mutex unless noted) ------------------------
    mutable std::mutex m_Mutex;
    DBusConnection *m_Conn = nullptr;
    std::thread m_Thread;
    std::atomic<bool> m_ThreadRunning{false};
    std::atomic<bool> m_StopRequested{false};

    std::string m_AdapterPath; // e.g. "/org/bluez/hci0"
    bool m_AgentRegistered = false;

    bool m_Discovering = false;
    std::chrono::steady_clock::time_point m_DiscoveryDeadline;
    WiimotePairing::DeviceFoundCallback m_OnDevice;
    WiimotePairing::DiscoveryDoneCallback m_OnDiscoveryDone;
    // object_path -> last-reported DiscoveredDevice, so we only fire
    // on_device again when something actually changed.
    std::map<std::string, DiscoveredDevice> m_KnownDevices;

    // Outstanding pairing/connect attempt, if any (only one at a time -
    // PairDevice() and ConnectDevice() share this same slot).
    std::string m_PairingObjectPath;
    WiimotePairing::PairCallback m_OnPairDone;
    DBusPendingCall *m_PairPending = nullptr;
    // True if the outstanding attempt is a ConnectDevice() (Device1.Connect,
    // no bond) rather than a PairDevice() (Device1.Pair, permanent bond) -
    // read by HandlePairReply() to pick the right success/error handling
    // for each. See ConnectDevice()'s comment.
    bool m_ConnectOnly = false;

    // Cross-thread event queue drained by Pump() on the caller's thread.
    // Each event carries its own copy of the callback to invoke rather
    // than Pump() reading it back out of m_On*/m_PairingObjectPath at
    // drain time - those members can legitimately change (a new discovery
    // scan starts, a pairing attempt is rejected outright because another
    // is already in flight) before Pump() next runs, and an event must
    // always invoke the callback that was current *when it was queued*.
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
    std::deque<QueuedEvent> m_EventQueue;
};

} // namespace InputBridge::Bluetooth

#endif // __linux__
