#include "App/Log.h"
#include "LinuxWiimoteBluetoothPairing.h"

#ifdef __linux__

#include <cstring>

static constexpr const char *kTag = "LinuxWiimoteBluetoothPairing";
static constexpr const char *kAgentPath = "/org/inputbridge/agent";
static constexpr int kDiscoverySeconds = 20; // matches the Wiimote's own sync window

namespace InputBridge::Bluetooth {

// --- small D-Bus parsing helpers (file-local) --------------------------------

namespace {

// `ifaces_dict` must already be positioned at the first entry of an
// "a{sa{sv}}" dict (i.e. after one dbus_message_iter_recurse() past the
// outer array). On a match, `*out_props` is left positioned at the first
// entry of that interface's "a{sv}" property dict (or is an "invalid"
// iterator if the interface has no properties - callers must handle that
// the same way as "property not found").
bool DictHasInterface(DBusMessageIter *ifaces_dict, const char *iface_name, DBusMessageIter *out_props) {
    while (dbus_message_iter_get_arg_type(ifaces_dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(ifaces_dict, &entry);
        const char *name = nullptr;
        dbus_message_iter_get_basic(&entry, &name);
        dbus_message_iter_next(&entry); // now at this interface's a{sv} props
        if (name && std::strcmp(name, iface_name) == 0) {
            DBusMessageIter props;
            dbus_message_iter_recurse(&entry, &props);
            *out_props = props;
            return true;
        }
        dbus_message_iter_next(ifaces_dict);
    }
    return false;
}

// `props_dict` must be positioned at the first entry of an "a{sv}" dict (a
// copy is taken since scanning mutates the iterator).
std::optional<std::string> GetStringProp(DBusMessageIter props_dict, const char *key) {
    while (dbus_message_iter_get_arg_type(&props_dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&props_dict, &entry);
        const char *k = nullptr;
        dbus_message_iter_get_basic(&entry, &k);
        dbus_message_iter_next(&entry); // now at the variant
        if (k && std::strcmp(k, key) == 0 &&
            dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
            DBusMessageIter variant;
            dbus_message_iter_recurse(&entry, &variant);
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                const char *s = nullptr;
                dbus_message_iter_get_basic(&variant, &s);
                return std::string(s ? s : "");
            }
            return std::nullopt;
        }
        dbus_message_iter_next(&props_dict);
    }
    return std::nullopt;
}

std::optional<bool> GetBoolProp(DBusMessageIter props_dict, const char *key) {
    while (dbus_message_iter_get_arg_type(&props_dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&props_dict, &entry);
        const char *k = nullptr;
        dbus_message_iter_get_basic(&entry, &k);
        dbus_message_iter_next(&entry);
        if (k && std::strcmp(k, key) == 0 &&
            dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
            DBusMessageIter variant;
            dbus_message_iter_recurse(&entry, &variant);
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN) {
                dbus_bool_t b = FALSE;
                dbus_message_iter_get_basic(&variant, &b);
                return b != FALSE;
            }
            return std::nullopt;
        }
        dbus_message_iter_next(&props_dict);
    }
    return std::nullopt;
}

} // namespace

// --- construction / teardown -------------------------------------------------

LinuxWiimoteBluetoothPairing::LinuxWiimoteBluetoothPairing() {
    DBusError err;
    dbus_error_init(&err);
    // A private connection (as opposed to dbus_bus_get()'s process-wide
    // shared one) so this class fully owns its lifecycle - closing it in
    // our destructor can't yank the connection out from under some other,
    // unrelated D-Bus user in the process.
    m_Conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (!m_Conn) {
        LOG_WARN(kTag, "Could not connect to the D-Bus system bus: %s",
                 dbus_error_is_set(&err) ? err.message : "unknown error");
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        return;
    }
    dbus_connection_set_exit_on_disconnect(m_Conn, FALSE);
    dbus_connection_add_filter(m_Conn, &LinuxWiimoteBluetoothPairing::MessageFilterThunk, this, nullptr);

    dbus_bus_add_match(m_Conn,
        "type='signal',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'", &err);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); dbus_error_init(&err); }
    dbus_bus_add_match(m_Conn,
        "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
        "arg0='org.bluez.Device1'", &err);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); dbus_error_init(&err); }

    if (!FindAdapterPath()) {
        LOG_WARN(kTag, "No BlueZ Bluetooth adapter found - Wiimote pairing unavailable "
                       "(is bluetoothd running and is an adapter present/powered?).");
        // Keep m_Conn open - IsAvailable() will report false via the empty
        // adapter path, and a later retry (e.g. user plugs in a USB
        // Bluetooth dongle) isn't handled automatically, matching how
        // WiimoteManager itself expects to be re-invoked rather than
        // watching for hardware appearing.
    }

    EnsureAdapterPowered();
    RegisterAgent();

    m_ThreadRunning = true;
    m_Thread = std::thread(&LinuxWiimoteBluetoothPairing::ThreadMain, this);
}

LinuxWiimoteBluetoothPairing::~LinuxWiimoteBluetoothPairing() {
    m_StopRequested = true;
    if (m_Thread.joinable()) m_Thread.join();
    if (m_Conn) {
        dbus_connection_remove_filter(m_Conn, &LinuxWiimoteBluetoothPairing::MessageFilterThunk, this);
        dbus_connection_close(m_Conn);
        dbus_connection_unref(m_Conn);
    }
}

bool LinuxWiimoteBluetoothPairing::IsAvailable() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Conn != nullptr && !m_AdapterPath.empty();
}

// --- adapter / agent setup ----------------------------------------------------

bool LinuxWiimoteBluetoothPairing::FindAdapterPath() {
    DBusMessage *call = dbus_message_new_method_call(
        "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, call, 5000, &err);
    dbus_message_unref(call);
    if (!reply) {
        if (dbus_error_is_set(&err)) {
            LOG_WARN(kTag, "GetManagedObjects failed: %s", err.message);
            dbus_error_free(&err);
        }
        return false;
    }
    ProcessManagedObjectsReply(reply);
    dbus_message_unref(reply);
    std::lock_guard<std::mutex> lock(m_Mutex);
    return !m_AdapterPath.empty();
}

void LinuxWiimoteBluetoothPairing::ProcessManagedObjectsReply(DBusMessage *reply) {
    DBusMessageIter it;
    if (!dbus_message_iter_init(reply, &it)) return;
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return;
    DBusMessageIter objects;
    dbus_message_iter_recurse(&it, &objects);

    while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter obj_entry;
        dbus_message_iter_recurse(&objects, &obj_entry);
        const char *object_path = nullptr;
        dbus_message_iter_get_basic(&obj_entry, &object_path);
        dbus_message_iter_next(&obj_entry); // now at the a{sa{sv}} interfaces array

        if (object_path && dbus_message_iter_get_arg_type(&obj_entry) == DBUS_TYPE_ARRAY) {
            DBusMessageIter ifaces;
            dbus_message_iter_recurse(&obj_entry, &ifaces);

            DBusMessageIter ifaces_for_adapter = ifaces;
            DBusMessageIter adapter_props;
            if (DictHasInterface(&ifaces_for_adapter, "org.bluez.Adapter1", &adapter_props)) {
                std::lock_guard<std::mutex> lock(m_Mutex);
                if (m_AdapterPath.empty()) m_AdapterPath = object_path;
            }

            DBusMessageIter ifaces_for_device = ifaces;
            DBusMessageIter device_props;
            if (DictHasInterface(&ifaces_for_device, "org.bluez.Device1", &device_props)) {
                HandleDeviceProperties(object_path, device_props);
            }
        }
        dbus_message_iter_next(&objects);
    }
}

bool LinuxWiimoteBluetoothPairing::EnsureAdapterPowered() {
    std::string adapter_path;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        adapter_path = m_AdapterPath;
    }
    if (adapter_path.empty()) return false;

    DBusMessage *call = dbus_message_new_method_call(
        "org.bluez", adapter_path.c_str(), "org.freedesktop.DBus.Properties", "Set");
    const char *iface = "org.bluez.Adapter1";
    const char *prop = "Powered";
    DBusMessageIter args, variant;
    dbus_message_iter_init_append(call, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &prop);
    dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_bool_t powered = TRUE;
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &powered);
    dbus_message_iter_close_container(&args, &variant);

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, call, 5000, &err);
    dbus_message_unref(call);
    bool ok = reply != nullptr;
    if (reply) dbus_message_unref(reply);
    if (dbus_error_is_set(&err)) {
        // Not fatal - the adapter may already be powered (BlueZ doesn't
        // error on a no-op Set either way, but be defensive), or the user
        // may not have permission to power it on, in which case pairing
        // will simply fail later with a clearer NotAvailable/Error result.
        LOG_WARN(kTag, "Set Powered=true failed: %s", err.message);
        dbus_error_free(&err);
    }
    return ok;
}

bool LinuxWiimoteBluetoothPairing::RegisterAgent() {
    if (!m_Conn) return false;

    // Capability "NoInputNoOutput": tells BlueZ our agent has no display
    // and no keyboard, matching a Wiimote exactly (see
    // WiimoteBluetoothPairing.h's header comment). Note this doesn't mean
    // pairing needs no PIN at all - see HandleAgentMethodCall below for
    // why RequestPinCode still gets called and what we do about it.
    DBusMessage *call = dbus_message_new_method_call(
        "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent");
    const char *path = kAgentPath;
    const char *capability = "NoInputNoOutput";
    dbus_message_append_args(call, DBUS_TYPE_OBJECT_PATH, &path,
                              DBUS_TYPE_STRING, &capability, DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, call, 5000, &err);
    dbus_message_unref(call);
    bool registered = reply != nullptr;
    if (reply) dbus_message_unref(reply);
    if (dbus_error_is_set(&err)) {
        // AlreadyExists just means a previous instance of this process (or
        // a crash-and-restart) already registered - harmless.
        if (!std::strstr(err.name, "AlreadyExists")) {
            LOG_WARN(kTag, "RegisterAgent failed: %s", err.message);
        } else {
            registered = true;
        }
        dbus_error_free(&err);
    }

    if (registered) {
        DBusMessage *req_default = dbus_message_new_method_call(
            "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent");
        dbus_message_append_args(req_default, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
        DBusError err2;
        dbus_error_init(&err2);
        DBusMessage *reply2 = dbus_connection_send_with_reply_and_block(m_Conn, req_default, 5000, &err2);
        dbus_message_unref(req_default);
        if (reply2) dbus_message_unref(reply2);
        if (dbus_error_is_set(&err2)) {
            LOG_WARN(kTag, "RequestDefaultAgent failed: %s", err2.message);
            dbus_error_free(&err2);
        }
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    m_AgentRegistered = registered;
    return registered;
}

// --- discovery -----------------------------------------------------------------

void LinuxWiimoteBluetoothPairing::StartDiscovery(WiimotePairing::DeviceFoundCallback on_device,
                                                   WiimotePairing::DiscoveryDoneCallback on_done) {
    if (!m_Conn) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::DiscoveryDone;
        ev.discovery_done_cb = std::move(on_done);
        ev.timed_out = false;
        m_EventQueue.push_back(std::move(ev));
        return;
    }

    std::string adapter_path;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        adapter_path = m_AdapterPath;
        m_OnDevice = std::move(on_device);
        m_OnDiscoveryDone = std::move(on_done);
        m_KnownDevices.clear();
        m_Discovering = true;
        m_DiscoveryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(kDiscoverySeconds);
    }

    if (adapter_path.empty()) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Discovering = false;
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::DiscoveryDone;
        ev.discovery_done_cb = m_OnDiscoveryDone;
        ev.timed_out = false;
        m_EventQueue.push_back(std::move(ev));
        return;
    }

    // Pick up anything BlueZ already knows about (previously-paired
    // devices, or ones another app's scan already surfaced) immediately,
    // rather than waiting for a fresh InterfacesAdded signal for each.
    {
        DBusMessage *call = dbus_message_new_method_call(
            "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, call, 5000, nullptr);
        dbus_message_unref(call);
        if (reply) {
            ProcessManagedObjectsReply(reply);
            dbus_message_unref(reply);
        }
    }

    DBusMessage *call = dbus_message_new_method_call(
        "org.bluez", adapter_path.c_str(), "org.bluez.Adapter1", "StartDiscovery");
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_Conn, call, 5000, &err);
    dbus_message_unref(call);
    if (reply) dbus_message_unref(reply);
    if (dbus_error_is_set(&err)) {
        // "InProgress" just means discovery (from us or another client, e.g.
        // the desktop's own Bluetooth settings panel) is already running -
        // that's fine, we still get the InterfacesAdded/PropertiesChanged
        // signals either way.
        bool already_in_progress = std::strstr(err.name, "InProgress") != nullptr;
        if (!already_in_progress) {
            LOG_WARN(kTag, "StartDiscovery failed: %s", err.message);
        }
        dbus_error_free(&err);
    }
}

void LinuxWiimoteBluetoothPairing::StopDiscovery() {
    std::string adapter_path;
    WiimotePairing::DiscoveryDoneCallback cb;
    bool was_discovering = false;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        was_discovering = m_Discovering;
        m_Discovering = false;
        adapter_path = m_AdapterPath;
        cb = m_OnDiscoveryDone;
    }
    if (!was_discovering) return;

    if (m_Conn && !adapter_path.empty()) {
        DBusMessage *call = dbus_message_new_method_call(
            "org.bluez", adapter_path.c_str(), "org.bluez.Adapter1", "StopDiscovery");
        // Fire-and-forget - whether or not this succeeds, we've already
        // stopped reporting devices on our side, and BlueZ will stop
        // discovery on its own shortly if some other client isn't also
        // using it.
        dbus_message_set_no_reply(call, TRUE);
        dbus_connection_send(m_Conn, call, nullptr);
        dbus_message_unref(call);
    }

    std::lock_guard<std::mutex> lock(m_Mutex);
    QueuedEvent ev;
    ev.kind = QueuedEvent::Kind::DiscoveryDone;
    ev.discovery_done_cb = cb;
    ev.timed_out = false;
    m_EventQueue.push_back(std::move(ev));
}

bool LinuxWiimoteBluetoothPairing::IsDiscovering() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Discovering;
}

// --- pairing -------------------------------------------------------------------

void LinuxWiimoteBluetoothPairing::PairDevice(const std::string &address, WiimotePairing::PairCallback on_done) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_Conn || m_AdapterPath.empty()) {
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::NotAvailable;
        ev.detail = "Bluetooth is not available.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }
    if (!m_PairingObjectPath.empty()) {
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::Error;
        ev.detail = "Another pairing attempt is already in progress.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }

    auto known = m_KnownDevices.find(address);
    if (known == m_KnownDevices.end()) {
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::NotFound;
        ev.detail = "Device is no longer known - try scanning again.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }
    if (known->second.already_paired) {
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::AlreadyPaired;
        m_EventQueue.push_back(std::move(ev));
        return;
    }

    m_PairingObjectPath = address; // `address` is the D-Bus object path (see header)
    m_OnPairDone = std::move(on_done);
    m_ConnectOnly = false;

    DBusMessage *call = dbus_message_new_method_call(
        "org.bluez", address.c_str(), "org.bluez.Device1", "Pair");
    DBusPendingCall *pending = nullptr;
    // 30s: generous margin over the ~20s sync window, since the user may
    // have pressed sync slightly before clicking "Pair" in the UI.
    if (!dbus_connection_send_with_reply(m_Conn, call, &pending, 30000) || !pending) {
        dbus_message_unref(call);
        WiimotePairing::PairCallback cb = std::move(m_OnPairDone);
        m_OnPairDone = nullptr;
        m_PairingObjectPath.clear();
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(cb);
        ev.pair_result = PairResult::Error;
        ev.detail = "Failed to send Pair request.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }
    dbus_message_unref(call);
    m_PairPending = pending;
    dbus_pending_call_set_notify(pending, &LinuxWiimoteBluetoothPairing::PairReplyThunk, this, nullptr);
}

void LinuxWiimoteBluetoothPairing::ConnectDevice(const std::string &address, WiimotePairing::PairCallback on_done) {
    // Deliberately calls Device1.Connect() directly instead of Device1.Pair()
    // - this is the "session-only, no permanent bond" path for a
    // 1+2-discovered Wiimote. See WiimoteBluetoothPairing.h's
    // ConnectDevice() comment for why PairDevice()/a permanent bond
    // doesn't work for these, and HandlePairReply()'s handling of
    // m_ConnectOnly below for the ClassicBondedOnly/CVE-2023-45866 caveat
    // this can still run into even when the Bluetooth-level connect
    // itself succeeds.
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!m_Conn || m_AdapterPath.empty()) {
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::NotAvailable;
        ev.detail = "Bluetooth is not available.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }
    if (!m_PairingObjectPath.empty()) {
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::Error;
        ev.detail = "Another pairing/connect attempt is already in progress.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }

    auto known = m_KnownDevices.find(address);
    if (known == m_KnownDevices.end()) {
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(on_done);
        ev.pair_result = PairResult::NotFound;
        ev.detail = "Device is no longer known - try scanning again.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }
    // Unlike PairDevice(), already_paired isn't treated as an early-exit
    // success case here - a permanently-bonded device should just use the
    // normal Pair() path (which itself already reports AlreadyPaired), and
    // this method is specifically for devices that aren't bonded at all.

    m_PairingObjectPath = address;
    m_OnPairDone = std::move(on_done);
    m_ConnectOnly = true;

    DBusMessage *call = dbus_message_new_method_call(
        "org.bluez", address.c_str(), "org.bluez.Device1", "Connect");
    DBusPendingCall *pending = nullptr;
    if (!dbus_connection_send_with_reply(m_Conn, call, &pending, 30000) || !pending) {
        dbus_message_unref(call);
        WiimotePairing::PairCallback cb = std::move(m_OnPairDone);
        m_OnPairDone = nullptr;
        m_PairingObjectPath.clear();
        m_ConnectOnly = false;
        QueuedEvent ev;
        ev.kind = QueuedEvent::Kind::PairDone;
        ev.pair_cb = std::move(cb);
        ev.pair_result = PairResult::Error;
        ev.detail = "Failed to send Connect request.";
        m_EventQueue.push_back(std::move(ev));
        return;
    }
    dbus_message_unref(call);
    m_PairPending = pending;
    dbus_pending_call_set_notify(pending, &LinuxWiimoteBluetoothPairing::PairReplyThunk, this, nullptr);
}

void LinuxWiimoteBluetoothPairing::PairReplyThunk(DBusPendingCall *pending, void *user_data) {
    static_cast<LinuxWiimoteBluetoothPairing *>(user_data)->HandlePairReply(pending);
}

void LinuxWiimoteBluetoothPairing::HandlePairReply(DBusPendingCall *pending) {
    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    bool connect_only;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_PairPending == pending) m_PairPending = nullptr;
        connect_only = m_ConnectOnly;
    }
    if (!reply) {
        FinishPairing(PairResult::Error, "No reply from bluetoothd.");
        return;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        const char *err_name = dbus_message_get_error_name(reply);
        std::string detail = err_name ? err_name : "unknown error";
        DBusMessageIter it;
        if (dbus_message_iter_init(reply, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) {
            const char *msg = nullptr;
            dbus_message_iter_get_basic(&it, &msg);
            if (msg && *msg) detail = msg;
        }

        PairResult result = PairResult::Error;
        if (err_name) {
            if (std::strstr(err_name, "AlreadyExists") || std::strstr(err_name, "AlreadyConnected")) {
                result = connect_only ? PairResult::Success : PairResult::AlreadyPaired;
            } else if (std::strstr(err_name, "AuthenticationCanceled") ||
                       std::strstr(err_name, "AuthenticationRejected") ||
                       std::strstr(err_name, "ConnectionAttemptFailed")) {
                result = PairResult::Rejected;
                if (connect_only) {
                    // A 1+2-discovered Wiimote failing here is exactly the
                    // known BlueZ ClassicBondedOnly=true (CVE-2023-45866
                    // fix) interaction described in
                    // WiimoteBluetoothPairing.h's ConnectDevice() comment -
                    // surface that context directly rather than just the
                    // raw D-Bus error name, since "AuthenticationRejected"
                    // alone gives no hint that a system security setting
                    // is the likely cause.
                    detail += " (if this is a Wiimote synced via 1+2, BlueZ's HID "
                        "profile refuses unbonded input devices by default "
                        "since BlueZ 5.66/CVE-2023-45866 - see "
                        "/etc/bluetooth/input.conf's ClassicBondedOnly "
                        "setting; changing it is a real security tradeoff, "
                        "not something this app changes for you)";
                }
            } else if (std::strstr(err_name, "AuthenticationTimeout")) {
                result = PairResult::Timeout;
            } else if (std::strstr(err_name, "DoesNotExist") || std::strstr(err_name, "NotFound")) {
                result = PairResult::NotFound;
            }
        }
        dbus_message_unref(reply);

        if (result == PairResult::AlreadyPaired) {
            ContinueToTrustAndConnect();
            FinishPairing(PairResult::AlreadyPaired, "");
        } else {
            FinishPairing(result, detail);
        }
        return;
    }

    dbus_message_unref(reply);
    if (!connect_only) {
        ContinueToTrustAndConnect();
    }
    // For connect_only, deliberately no Trust/re-Connect follow-up -
    // ConnectDevice() already achieved exactly what it's meant to (a
    // connection, nothing persisted) and calling ContinueToTrustAndConnect()
    // would set Trusted=true, which - while not itself a permanent *bond* -
    // isn't part of what "session-only" is supposed to mean here either.
    FinishPairing(PairResult::Success, "");
}

void LinuxWiimoteBluetoothPairing::ContinueToTrustAndConnect() {
    std::string object_path;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        object_path = m_PairingObjectPath;
    }
    if (object_path.empty() || !m_Conn) return;

    // Trust it so BlueZ/the desktop's own Bluetooth agent won't prompt for
    // confirmation on future reconnects (e.g. after the Wiimote sleeps and
    // is woken by a button press) now that we've paired it ourselves.
    {
        DBusMessage *call = dbus_message_new_method_call(
            "org.bluez", object_path.c_str(), "org.freedesktop.DBus.Properties", "Set");
        const char *iface = "org.bluez.Device1";
        const char *prop = "Trusted";
        DBusMessageIter args, variant;
        dbus_message_iter_init_append(call, &args);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &prop);
        dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "b", &variant);
        dbus_bool_t trusted = TRUE;
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &trusted);
        dbus_message_iter_close_container(&args, &variant);
        // Best-effort - failure here doesn't change whether pairing itself
        // succeeded, only future reconnect convenience.
        dbus_message_set_no_reply(call, TRUE);
        dbus_connection_send(m_Conn, call, nullptr);
        dbus_message_unref(call);
    }

    // Explicitly connect so the HID profile comes up immediately rather
    // than waiting on BlueZ's own auto-reconnect timing - WiimoteManager's
    // periodic Scan() (see its header) will then find the resulting hidraw
    // node on its very next pass instead of however long BlueZ takes.
    {
        DBusMessage *call = dbus_message_new_method_call(
            "org.bluez", object_path.c_str(), "org.bluez.Device1", "Connect");
        dbus_message_set_no_reply(call, TRUE);
        dbus_connection_send(m_Conn, call, nullptr);
        dbus_message_unref(call);
    }
}

void LinuxWiimoteBluetoothPairing::FinishPairing(PairResult result, const std::string &detail) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    QueuedEvent ev;
    ev.kind = QueuedEvent::Kind::PairDone;
    ev.pair_cb = m_OnPairDone;
    ev.pair_result = result;
    ev.detail = detail;
    m_EventQueue.push_back(std::move(ev));
    m_OnPairDone = nullptr;
    m_PairingObjectPath.clear();
    m_ConnectOnly = false;
}

// --- device bookkeeping ----------------------------------------------------

void LinuxWiimoteBluetoothPairing::HandleDeviceProperties(const std::string &object_path,
                                                            DBusMessageIter &props_dict_iter) {
    auto name_opt = GetStringProp(props_dict_iter, "Name");
    if (!name_opt || name_opt->empty()) name_opt = GetStringProp(props_dict_iter, "Alias");
    auto paired_opt = GetBoolProp(props_dict_iter, "Paired");

    DiscoveredDevice dev;
    dev.address = object_path;
    dev.name = name_opt.value_or("");
    dev.already_paired = paired_opt.value_or(false);
    dev.looks_like_wiimote = IsWiimoteBluetoothName(dev.name);

    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Discovering) return; // see HandlePropertiesChanged's comment
    auto it = m_KnownDevices.find(object_path);
    if (it != m_KnownDevices.end() && it->second.name == dev.name &&
        it->second.already_paired == dev.already_paired) {
        return; // nothing changed - don't spam the UI
    }
    m_KnownDevices[object_path] = dev;

    QueuedEvent ev;
    ev.kind = QueuedEvent::Kind::DeviceFound;
    ev.device_cb = m_OnDevice;
    ev.device = dev;
    m_EventQueue.push_back(std::move(ev));
}

void LinuxWiimoteBluetoothPairing::HandleInterfacesAdded(DBusMessage *msg) {
    DBusMessageIter args;
    if (!dbus_message_iter_init(msg, &args)) return;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH) return;
    const char *object_path = nullptr;
    dbus_message_iter_get_basic(&args, &object_path);
    if (!object_path) return;
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) return;

    DBusMessageIter ifaces;
    dbus_message_iter_recurse(&args, &ifaces);
    DBusMessageIter device_props;
    if (DictHasInterface(&ifaces, "org.bluez.Device1", &device_props)) {
        HandleDeviceProperties(object_path, device_props);
    }
}

void LinuxWiimoteBluetoothPairing::HandlePropertiesChanged(DBusMessage *msg) {
    DBusMessageIter args;
    if (!dbus_message_iter_init(msg, &args)) return;
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return;
    const char *iface = nullptr;
    dbus_message_iter_get_basic(&args, &iface);
    if (!iface || std::strcmp(iface, "org.bluez.Device1") != 0) return;
    dbus_message_iter_next(&args); // changed properties: a{sv}
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) return;

    const char *object_path = dbus_message_get_path(msg);
    if (!object_path) return;

    // Unlike InterfacesAdded/GetManagedObjects (a full property snapshot),
    // this signal only carries whatever changed - merge onto whatever we
    // already know about this device rather than treating missing keys as
    // "cleared".
    DBusMessageIter changed;
    dbus_message_iter_recurse(&args, &changed);
    auto name_opt = GetStringProp(changed, "Name");
    if (!name_opt) name_opt = GetStringProp(changed, "Alias");
    auto paired_opt = GetBoolProp(changed, "Paired");
    if (!name_opt && !paired_opt) return; // nothing we track changed

    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Discovering) return; // only care about live updates during a scan

    DiscoveredDevice dev;
    auto it = m_KnownDevices.find(object_path);
    if (it != m_KnownDevices.end()) dev = it->second;
    else dev.address = object_path;

    bool changed_anything = false;
    if (name_opt && *name_opt != dev.name) { dev.name = *name_opt; changed_anything = true; }
    if (paired_opt && *paired_opt != dev.already_paired) { dev.already_paired = *paired_opt; changed_anything = true; }
    if (!changed_anything) return;

    dev.looks_like_wiimote = IsWiimoteBluetoothName(dev.name);
    m_KnownDevices[object_path] = dev;

    QueuedEvent ev;
    ev.kind = QueuedEvent::Kind::DeviceFound;
    ev.device_cb = m_OnDevice;
    ev.device = dev;
    m_EventQueue.push_back(std::move(ev));
}

// --- Agent1 (see class comment / RegisterAgent) --------------------------------

void LinuxWiimoteBluetoothPairing::HandleAgentMethodCall(DBusMessage *msg) {
    const char *member = dbus_message_get_member(msg);
    if (!member || !m_Conn) return;

    DBusMessage *reply = nullptr;

    if (std::strcmp(member, "Release") == 0 ||
        std::strcmp(member, "Cancel") == 0) {
        reply = dbus_message_new_method_return(msg);
    } else if (std::strcmp(member, "RequestConfirmation") == 0 ||
               std::strcmp(member, "RequestAuthorization") == 0 ||
               std::strcmp(member, "AuthorizeService") == 0) {
        // Empty success reply = accept. A NoInputNoOutput agent shouldn't
        // normally be asked RequestConfirmation for a Just Works pairing,
        // but auto-accept defensively rather than leave BlueZ hanging if
        // it is.
        reply = dbus_message_new_method_return(msg);
    } else {
        // RequestPinCode / DisplayPinCode / DisplayPasskey / RequestPasskey:
        // reply with an error instead of leaving BlueZ hanging.
        //
        // RequestPinCode used to attempt WiiBrew's documented trick for
        // legacy (pre-SSP) Wiimote pairing - answer with the host
        // adapter's own Bluetooth address, reversed. That's fundamentally
        // impossible to implement correctly over this D-Bus method: the
        // Agent1 API represents the PIN as a DBUS_TYPE_STRING, and D-Bus
        // requires STRING arguments to be valid UTF-8 on the wire - but a
        // reversed 6-byte Bluetooth address is arbitrary binary that is
        // essentially never valid UTF-8 (any byte >= 0x80 not part of a
        // proper multi-byte sequence is a hard rejection). Attempting it
        // hit exactly that: dbus_message_append_args() asserts and
        // SIGABRTs the whole process on an invalid-UTF-8 string, which is
        // worse than just declining - so this method now always declines.
        //
        // This isn't a real-world loss for most people: modern BlueZ ships
        // its own built-in "wiimote" plugin that already answers this PIN
        // internally at the daemon level, below the Agent1 D-Bus API
        // entirely, for recognized Wiimote controllers (see the Arch Linux
        // forum thread on Wiimote pairing: "This plugin automatically
        // selects the right PIN for the wiimote regardless of the agent
        // you use"). This RequestPinCode being reached at all - as opposed
        // to BlueZ's own plugin already having answered it before we ever
        // see a request - means that plugin isn't handling this particular
        // controller/BlueZ build, and legacy-pairing will fail here with a
        // clear Rejected result rather than crash. Real-world reports of
        // Wiimote pairing being flaky even independent of any particular
        // app (bluez/bluez#765, bluez/bluez#1089) suggest this remains a
        // genuinely finicky area of BlueZ itself.
        reply = dbus_message_new_error(msg, "org.bluez.Error.Rejected", "not supported by this agent");
    }

    if (reply) {
        dbus_connection_send(m_Conn, reply, nullptr);
        dbus_message_unref(reply);
    }
}

// --- message filter / background thread ----------------------------------------

DBusHandlerResult LinuxWiimoteBluetoothPairing::MessageFilterThunk(DBusConnection *, DBusMessage *msg,
                                                                     void *user_data) {
    return static_cast<LinuxWiimoteBluetoothPairing *>(user_data)->HandleMessage(msg);
}

DBusHandlerResult LinuxWiimoteBluetoothPairing::HandleMessage(DBusMessage *msg) {
    if (dbus_message_is_signal(msg, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded")) {
        HandleInterfacesAdded(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
        HandlePropertiesChanged(msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        const char *path = dbus_message_get_path(msg);
        if (path && std::strcmp(path, kAgentPath) == 0) {
            HandleAgentMethodCall(msg);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void LinuxWiimoteBluetoothPairing::ThreadMain() {
    while (!m_StopRequested) {
        // Short timeout so we can also notice the discovery window
        // elapsing and m_StopRequested being set without waiting
        // indefinitely inside libdbus - this is a background thread doing
        // nothing else, so a 200ms poll granularity costs nothing and
        // keeps the "scan lasts ~20s" promise reasonably tight.
        dbus_connection_read_write_dispatch(m_Conn, 200);

        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Discovering && std::chrono::steady_clock::now() >= m_DiscoveryDeadline) {
            m_Discovering = false;
            if (!m_AdapterPath.empty() && m_Conn) {
                DBusMessage *call = dbus_message_new_method_call(
                    "org.bluez", m_AdapterPath.c_str(), "org.bluez.Adapter1", "StopDiscovery");
                dbus_message_set_no_reply(call, TRUE);
                dbus_connection_send(m_Conn, call, nullptr);
                dbus_message_unref(call);
            }
            QueuedEvent ev;
            ev.kind = QueuedEvent::Kind::DiscoveryDone;
            ev.discovery_done_cb = m_OnDiscoveryDone;
            ev.timed_out = true;
            m_EventQueue.push_back(std::move(ev));
        }
    }
    m_ThreadRunning = false;
}

// --- Pump ------------------------------------------------------------------

void LinuxWiimoteBluetoothPairing::Pump() {
    std::deque<QueuedEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        events.swap(m_EventQueue);
    }
    for (auto &ev : events) {
        switch (ev.kind) {
            case QueuedEvent::Kind::DeviceFound:
                if (ev.device_cb) ev.device_cb(ev.device);
                break;
            case QueuedEvent::Kind::DiscoveryDone:
                if (ev.discovery_done_cb) ev.discovery_done_cb(ev.timed_out);
                break;
            case QueuedEvent::Kind::PairDone:
                if (ev.pair_cb) ev.pair_cb(ev.pair_result, ev.detail);
                break;
        }
    }
}

} // namespace InputBridge::Bluetooth

#endif // __linux__
