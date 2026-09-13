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
    //
    // This is specifically for a SYNC-button-discovered device - see the
    // header comment above for why a 1+2-discovered device should go
    // through ConnectDevice() below instead, not this.
    void PairDevice(const std::string &address, PairCallback on_done);

    // Connects to a 1+2-discovered device for the current session only,
    // WITHOUT requesting a permanent OS-level bond - see the header
    // comment above for why PairDevice()/a permanent bond doesn't work for
    // a 1+2-discovered Wiimote (the controller itself refuses to store
    // one). The connection doesn't survive a reboot or this process
    // restarting; the person will need to hold 1+2 again next time.
    //
    // Important limitation this can't work around: on Linux, BlueZ's own
    // HID input profile has required the *device* to already be bonded by
    // default since BlueZ 5.66 (`ClassicBondedOnly=true` in
    // /etc/bluetooth/input.conf) - a deliberate security fix for
    // CVE-2023-45866 (a nearby attacker could otherwise inject a fake
    // Bluetooth keyboard/mouse). That means even a successful
    // ConnectDevice() may still not be usable as actual input unless the
    // person has explicitly opted into `ClassicBondedOnly=false` on their
    // system - this module deliberately does not flip that setting itself
    // (it's a system-wide security tradeoff, not something to change out
    // from under someone silently) and PairResult::Success here only
    // reflects that the Bluetooth-level connection succeeded, not that the
    // HID profile will accept it. See LinuxWiimoteBluetoothPairing.cpp's
    // ConnectDevice() for the full explanation to surface to the person if
    // this doesn't end up working end-to-end. Not implemented on every
    // platform - see each backend header.
    void ConnectDevice(const std::string &address, PairCallback on_done);

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
