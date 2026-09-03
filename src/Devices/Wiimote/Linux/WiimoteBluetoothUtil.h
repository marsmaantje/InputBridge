// src/Devices/Wiimote/Linux/WiimoteBluetoothUtil.h
//
// Small Linux-only helpers WiimoteManager needs to get from "here's a
// hidraw path SDL_hid_enumerate() found" to "here's a raw L2CAP
// connection to that same physical Wiimote" - see WiimoteL2CAPTransport.h
// for why that's worth doing at all.
#pragma once
#ifdef __linux__

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace InputBridge::Wiimote {

// Parses "AA:BB:CC:DD:EE:FF" (case-insensitive) into 6 raw bytes in the
// same human-reading order (out[0] == 0xAA above) - NOT the
// least-significant-byte-first order the kernel's L2CAP sockaddr wants;
// WiimoteL2CAPTransport::Connect() does that conversion itself. Returns
// std::nullopt if `text` isn't a well-formed address.
std::optional<std::array<uint8_t, 6>> ParseBluetoothAddress(const std::string &text);

// Given a hidraw device path (e.g. "/dev/hidraw3", the same string
// SDL_hid_device_info::path gives us), resolves the remote Bluetooth
// address of whatever's connected to it via that node's sysfs `uevent`
// file's `HID_UNIQ` field - which the kernel's Bluetooth HID transport
// (hidp) populates with exactly this for any Bluetooth HID device, no
// D-Bus/BlueZ userspace round-trip required. Returns std::nullopt if the
// path isn't a Bluetooth HID device (e.g. it's a USB Wiimote receiver, or
// the sysfs layout doesn't match what's expected on this kernel version)
// or the file can't be read.
std::optional<std::string> ResolveBluetoothAddressForHidrawPath(const std::string &hidraw_path);

// Best-effort: asks the OS's own Bluetooth HID connection to this device
// to disconnect, freeing up PSMs 0x11/0x13 so WiimoteL2CAPTransport can
// connect its own direct sockets to them - otherwise the OS's existing
// HID connection (the same one that created the hidraw node we resolved
// `address` from in the first place) is already holding those channels
// open, and our connect() will simply be refused.
//
// Implemented by shelling out to `bluetoothctl disconnect <address>`
// (BlueZ's own CLI, present on essentially any Linux system that has
// BlueZ installed at all) rather than linking against BlueZ's D-Bus API
// directly - avoids adding a libdbus build dependency for a single
// fire-and-forget call. This is a known simplification: it assumes
// `bluetoothctl` is on PATH and silently no-ops (logging a warning) if it
// isn't, rather than failing the whole connect attempt - the L2CAP
// connect below will just fail with a clear log line in that case, same
// as it would for any other reason the channels are unavailable.
void DisconnectExistingHidConnection(const std::string &address);

} // namespace InputBridge::Wiimote

#endif // __linux__
