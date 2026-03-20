# InputBridge

[![Build Status](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml)

InputBridge reads joystick, gamepad, steering wheel, and flight stick input and streams it over **OSC** and **WebSocket** to any receiving application. It also accepts haptic commands back — rumble, force feedback, adaptive triggers — and dispatches them to connected devices in real time.

---

## Supported Devices

| Device | Haptic Features |
|---|---|
| Generic Gamepad | Rumble (low / high frequency) |
| Steering Wheel | Constant force, periodic, condition effects (Spring / Damper / Inertia / Friction) |
| Flight Stick / Throttle | Constant force, periodic, condition on both pitch and roll axes |

---

## Quick Start

1. **Download** the latest build for your platform from [Releases](../../releases).
2. **Connect** your controller — it appears in the **Devices** tab automatically.
3. **Create a protocol** in the **Protocols** tab (or import a template from `protocols/templates/`).
4. **Map your axes** in the **Input** tab — click a field and move the physical axis.
5. **Start the server** in the **Network** tab and select your output protocol.

See the [Wiki](../../wiki) for a full step-by-step guide and reference documentation.

---

## Features

- **Protocol Editor** — define exactly which fields to send, with custom OSC paths and WebSocket keys. Import, export, duplicate, and version-control protocol files.
- **Mapping Profiles** — multiple named profiles, each with independent axis mappings, button mappings, server settings, and protocol selections.
- **Device Visibility** — hide a controller from all other applications while InputBridge keeps reading it (HidHide on Windows, evdev on Linux, IOKit on macOS).
- **Virtual Devices** — create simulated joysticks to test protocols without real hardware.
- **Undo / Redo** — 50-step history for all protocol editing operations.
- **Backup Manager** — automatic timestamped backups before every destructive change.
- **Themes** — VRChat, Resonite, Cyberpunk 2077, and custom JSON themes.

---

## Network

### OSC Server

Sends axis and button data over UDP. Defaults: send `127.0.0.1:9066`, receive `9068`.

Incoming haptic paths — both short and long forms are always active:

```
/haptic/rumble          iiffi   deviceId, slot, low_freq, high_freq, duration_ms
/haptic/constant        iifi    deviceId, slot, strength, duration_ms
/haptic/periodic        iiififfii  deviceId, slot, wave_type, strength, period, magnitude, offset, phase, duration_ms
/haptic/condition       iiiffffffi deviceId, slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms
/haptic/gain            ii      deviceId, gain (0–100)
```

Subchannel paths encode the slot in the URL (`/haptic/rumble/0`, `/haptic/rumble/1`, …) for hosts like Resonite that allow only one message per path per frame.

DualSense adaptive triggers use `/inputbridge/haptics/dualsense/trigger/{left|right}/{feedback|weapon|vibration|off}`.

### WebSocket Server

Accepts JSON haptic commands on port `4269` (default):

```json
{ "device": 1, "type": "gamepad", "effect": "rumble",
  "params": { "large_magnitude": 0.8, "small_magnitude": 0.2, "duration_ms": 500 } }
```

Supported types: `gamepad` (rumble, dualsense\_trigger), `steering_wheel` / `flight_stick` (constant, periodic, condition, gain), `wheel` (led\_rpm).

---

## Protocol System

Protocols are JSON files in `protocols/definitions/` next to the executable. Each one specifies a transport (`osc` / `websocket`), a direction (`output` / `input`), and a list of fields with their OSC paths or WebSocket keys.

```json
{
  "name": "Sim Racing OSC Output",
  "transport": "osc",
  "direction": "output",
  "osc": { "host": "127.0.0.1", "sendPort": 9066, "recvPort": 9068 },
  "fields": [
    { "fieldId": "axis_steering", "oscPath": "/input/steering", "enabled": true },
    { "fieldId": "axis_throttle", "oscPath": "/input/throttle", "enabled": true },
    { "fieldId": "axis_brake",    "oscPath": "/input/brake",    "enabled": true }
  ]
}
```

Custom fields can be added to `protocols/input_fields.json` and will appear in the Protocol Editor field picker. Reload at runtime via **Protocols → Reload Fields**.

---

## Building

**Requirements:** CMake 3.16+, C++20 compiler, Git (for submodules).

```bash
git clone --recursive https://github.com/marsmaantje/InputBridge.git
cd InputBridge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**CMake options:**

| Option | Default | Description |
|---|---|---|
| `ENABLE_EXCLUSIVE_INPUT` | ON | Device-hide support |
| `ENABLE_WEBSOCKETS` | ON | WebSocket server |
| `ENABLE_OSC` | ON | OSC server |
| `BUILD_TESTS` | OFF | Google Test suite |

Platform-specific dependency lists and packaging instructions are in the [Building from Source](../../wiki/Building-from-Source) wiki page.