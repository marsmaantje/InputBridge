// src/Bluetooth/WiimoteBluetoothPairing.h
//
// In-app Bluetooth Classic (BR/EDR) discovery + pairing for Wii Remotes,
// so a user doesn't have to leave InputBridge and use the OS's own
// Bluetooth settings just to add a Wiimote. Once a device is paired here,
// it shows up as a regular HID device and WiimoteManager::Scan() (see
// src/Devices/Wiimote/WiimoteManager.h) picks it up exactly like any
// Wiimote paired the old way - this module only replaces the "go pair it
// yourself first" step, it doesn't change how Wiimotes are driven
// afterwards.
//
// Background a caller needs to know to build a sane UI around this:
//
//   - Wiimotes only support Bluetooth Classic (BR/EDR), never BLE. A
//     Wiimote must be put into discoverable/pairing mode first: hold the
//     red SYNC button under the battery cover for ~1 second (original/Plus
//     remotes with a sync button), or hold buttons 1+2 together (remotes
//     without one). This makes it discoverable/connectable for ~20 seconds,
//     which is why StartDiscovery() below runs on roughly that timescale -
//     tell the user to press sync *before* they hit "Scan", not after.
//   - Pairing itself needs no PIN entry and no user confirmation on a
//     display, because Wiimotes use Secure Simple Pairing's "Just Works"
//     association model (no display, no keyboard on the remote side). Each
//     platform backend registers accordingly (BlueZ agent capability
//     "NoInputNoOutput", WinRT ConfirmOnly custom pairing, etc.) so no
//     backend should ever need to show the person a passkey/PIN prompt for
//     a genuine Wiimote. If a backend ever surfaces one, something's
//     talking to a different kind of device.
//   - This is entirely separate from OS-level Bluetooth-off/no-adapter/
//     permission-denied states, which IsAvailable() below folds into a
//     single false rather than trying to distinguish them for the caller -
//     PairResult::NotAvailable from PairDevice() carries a human-readable
//     `detail` string with the specifics when that happens instead.
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace InputBridge::Bluetooth {

class WiimotePairingImpl;

// One Bluetooth Classic device seen during discovery.
struct DiscoveredDevice {
    // Platform-native address token. Treat as opaque - it's whatever the
    // backend needs to find this device again in PairDevice(); don't parse
    // or reformat it (Linux uses a BlueZ D-Bus object path, Windows a WinRT
    // AEP id string, macOS an IOBluetoothDevice address string - none of
    // those are guaranteed to look like colon-separated hex).
    std::string address;

    // Advertised device name ("Nintendo RVL-CNT-01", "Nintendo
    // RVL-CNT-01-TR", "Nintendo RVL-WBC-01"). May briefly be empty right
    // after a device is first seen, before the name resolves.
    std::string name;

    bool already_paired = false;

    // True if `name` matched a known Wiimote/Wiimote Plus/Balance Board
    // pattern (see IsWiimoteBluetoothName() below). Backends still report
    // non-matching devices too (a scan naturally sees every discoverable
    // device nearby, not just Wiimotes) so the UI can show them grayed out
    // rather than have them silently vanish and look like the scan missed
    // something.
    bool looks_like_wiimote = false;
};

enum class PairResult {
    Success,
    AlreadyPaired,   // was already paired; treated as a successful outcome
    NotFound,        // address no longer known to the backend (e.g. device
                      // went back out of range/discoverable window closed)
    Rejected,        // remote device or OS pairing flow declined
    Timeout,
    NotAvailable,    // Bluetooth off / no adapter / permission denied
    Error,
};

const char *ToString(PairResult result);

// True if `name` is a known Wii Remote / Wii Remote Plus / Wii Balance
// Board Bluetooth advertised name. Shared across all backends and by the
// UI layer so "does this look like a Wiimote" is defined in exactly one
// place - kept in sync by hand with
// WiimoteManager::IsWiimoteProductString()'s HID product-string check,
// which matches the same two substrings for the same reason (see that
// function's comment).
bool IsWiimoteBluetoothName(const std::string &name);

// High-level in-app pairing manager. Not thread-safe - construct, use, and
// destroy on a single thread (the UI thread, in practice); backends may do
// their own work on background threads internally but always deliver
// callbacks back through Pump().
class WiimotePairing {
public:
    WiimotePairing();
    ~WiimotePairing();

    WiimotePairing(const WiimotePairing &) = delete;
    WiimotePairing &operator=(const WiimotePairing &) = delete;

    // False if this platform has no working backend compiled in, or the
    // backend detects up front that it can't function (e.g. required
    // system service isn't reachable). Individual transient failures
    // (adapter off right now, etc.) surface later through the callbacks
    // instead, since those can change between IsAvailable() and actually
    // trying.
    bool IsAvailable() const;

    using DeviceFoundCallback = std::function<void(const DiscoveredDevice &)>;
    using DiscoveryDoneCallback = std::function<void(bool timed_out)>;

    // Starts a scan lasting roughly the same ~20s window a synced Wiimote
    // stays discoverable for. `on_device` fires once per newly-seen device
    // and again whenever a previously-reported device's name or
    // already_paired state changes (e.g. its name resolves after being
    // initially empty). `on_done` fires exactly once when the scan ends,
    // whether because the window elapsed (timed_out=true) or StopDiscovery()
    // was called (timed_out=false). Calling StartDiscovery() again while
    // already discovering restarts the window.
    //
    // Both callbacks are delivered from Pump(), never directly from
    // StartDiscovery() or from a background thread - see Pump()'s comment.
    void StartDiscovery(DeviceFoundCallback on_device, DiscoveryDoneCallback on_done);
    void StopDiscovery();
    bool IsDiscovering() const;

    using PairCallback = std::function<void(PairResult, const std::string &detail)>;

    // Pairs (and, on success, connects/trusts so it's immediately usable)
    // the device previously reported via `on_device` with this `address`.
    // Asynchronous - result arrives through `on_done`, delivered from
    // Pump(). Safe to call while a discovery scan is still running.
    void PairDevice(const std::string &address, PairCallback on_done);

    // Must be called regularly (e.g. once per frame from the UI thread)
    // for as long as a scan or pairing attempt is outstanding. Every
    // backend does its actual OS Bluetooth work asynchronously (D-Bus
    // dispatch, WinRT's IAsyncOperation, IOBluetooth delegate callbacks on
    // whatever thread the OS chooses to invoke them on) and queues the
    // resulting callback rather than invoking it in place; Pump() is what
    // drains that queue and invokes callbacks on the calling thread, so
    // that e.g. an ImGui-touching callback here is always safe to call
    // ImGui from - callers should never call into ImGui (or anything else
    // that assumes the UI thread) directly from a StartDiscovery/PairDevice
    // callback under some other backend without Pump() being how it got
    // there. Cheap to call when nothing is pending.
    void Pump();

private:
    std::unique_ptr<WiimotePairingImpl> m_Impl;
};

} // namespace InputBridge::Bluetooth
