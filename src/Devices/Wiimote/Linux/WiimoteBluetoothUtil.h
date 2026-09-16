// src/Devices/Wiimote/Linux/WiimoteBluetoothUtil.h
//
// Small Linux-only helpers WiimoteManager needs to get from "here's a
// hidraw path SDL_hid_enumerate() found" to "here's a raw L2CAP connection
// to that same physical Wiimote" - see WiimoteL2CAPTransport.h for why.
#pragma once
#ifdef __linux__

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace InputBridge::Wiimote {

// Parses "AA:BB:CC:DD:EE:FF" (case-insensitive) into 6 bytes in
// human-reading order (out[0] == 0xAA) - NOT the little-endian order the
// kernel's L2CAP sockaddr wants; WiimoteL2CAPTransport::Connect() converts
// that itself. Returns nullopt if malformed.
std::optional<std::array<uint8_t, 6>> ParseBluetoothAddress(const std::string &text);

// Given a hidraw path (e.g. "/dev/hidraw3"), resolves the remote Bluetooth
// address via that node's sysfs `uevent` HID_UNIQ field - populated by the
// kernel's hidp transport for any Bluetooth HID device, no D-Bus/BlueZ
// round-trip needed. Returns nullopt if not a Bluetooth HID device (e.g. a
// USB receiver) or unreadable.
std::optional<std::string> ResolveBluetoothAddressForHidrawPath(const std::string &hidraw_path);

// Best-effort: asks the OS's Bluetooth HID connection to disconnect,
// freeing PSMs 0x11/0x13 for WiimoteL2CAPTransport's own sockets.
//
// NOT currently called from WiimoteManager's automatic Scan() path - doing
// so tears down the real ACL link, and several Wiimotes won't reconnect
// without the sync button being pressed again. Kept for a possible future
// user-initiated "take over this connection" action, not anything
// automatic - see WiimoteManager.cpp's TryOpenLinuxL2CAPTransport().
//
// Shells out to `bluetoothctl disconnect <address>` rather than linking
// BlueZ's D-Bus API, to avoid a libdbus dependency for one fire-and-forget
// call. If bluetoothctl isn't on PATH, logs a warning and no-ops - the
// L2CAP connect below just fails with its own clear log line.
void DisconnectExistingHidConnection(const std::string &address);

} // namespace InputBridge::Wiimote

#endif // __linux__
