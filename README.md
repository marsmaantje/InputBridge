# InputBridge

[![Build Status](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml) [![Test Results (Ubuntu)](https://github.com/marsmaantje/InputBridge/workflows/Test%20Results%20(ubuntu-latest)/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml) [![Test Results (Windows)](https://github.com/marsmaantje/InputBridge/workflows/Test%20Results%20(windows-latest)/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml) [![Test Results (macOS)](https://github.com/marsmaantje/InputBridge/workflows/Test%20Results%20(macos-latest)/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml)

InputBridge is a cross-platform input device bridge that relays joystick, gamepad, steering wheel, and other controller data over OSC and WebSocket. It supports haptic feedback control, adaptive trigger effects, a fully configurable protocol system, and per-device visibility control to hide physical devices from other applications while keeping them fully accessible to InputBridge.

---

## Supported Devices

| Device Type | Features |
|---|---|
| Generic Gamepad | Rumble (low/high frequency motors) |
| Xbox One / Series X\|S | Rumble + impulse trigger motors (left/right independently) |
| Sony DualSense (PS5) | Adaptive triggers (7 effect types), rumble, LED control, player indicators — USB & Bluetooth |
| Steering Wheel | Constant force, periodic effects, condition effects (Spring / Damper / Inertia / Friction) |
| Flight Stick / Throttle | Constant force, periodic, condition effects (applied on both pitch and roll axes), rumble |
| Wiimote | Button + axis visualisation |

---

## Device Visibility (Hide from Other Applications)

Each connected device has a **Device Visibility** section in its panel. Enabling **"Hide from other applications"** prevents any other process from receiving input from that device while InputBridge continues to read it normally. This is useful when you want a physical controller to be exclusively used by InputBridge — for example, when bridging inputs to a virtual device in VRChat or Resonite.

The hide is implemented using the best available mechanism per platform:

| Platform | Mechanism | Notes |
|---|---|---|
| **Windows** | [HidHide](https://github.com/nefarius/HidHide) kernel-mode filter driver | Must be installed separately. InputBridge is added to the allow-list automatically. See below for Steam Input compatibility. |
| **Linux** | `EVIOCGRAB` exclusive evdev grab + open `jsN` fd | Grabs both `eventN` and `jsN` nodes so no access path leaks events. Released automatically on exit. |
| **macOS** | `IOHIDOptionsTypeSeizeDevice` (IOKit) | Released automatically on exit. |

If the underlying mechanism is unavailable (e.g. HidHide is not installed on Windows), the checkbox is shown greyed out with an explanatory message.

### Steam Input Compatibility (Windows)

When using HidHide on Windows, an additional **"Keep Steam Input access"** checkbox is shown (enabled by default). When checked, `steam.exe` is added to the HidHide allow-list alongside InputBridge, so Steam Input features (gyro, haptics, controller glyphs, etc.) continue to work even while the device is hidden from every other application.

> **Note:** HidHide's block-list persists in the kernel driver across application restarts. Unchecking the hide toggle always removes the device from the block-list immediately. If InputBridge exits unexpectedly while a device is hidden, open the [HidHide Configuration Client](https://github.com/nefarius/HidHide) to clear the block-list manually.

---

## Protocol System

InputBridge uses a **Protocol Registry** to manage how data is sent and received. Protocols are stored as JSON files in a `protocols/` folder next to the application executable.

```
protocols/
├── input_fields.json          ← External field catalog (editable)
├── definitions/               ← One .json file per protocol definition
│   ├── <id1>.json
│   ├── <id2>.json
│   └── ...
└── README.md
```

Each protocol definition specifies:
- **Transport**: `osc` or `websocket`
- **Direction**: `output` (server → client, sends input/sensor data) or `input` (client → server, receives haptic/command data)
- **Fields**: which data fields to include, with optional OSC path and WebSocket key overrides

Protocols are created, edited, duplicated, imported, and exported via the **Protocol Editor**, accessible from the Network Server panel under the **Protocols** tab. Changes can be undone and redone (up to 50 steps) using the built-in undo/redo system.

### Protocol Definition Format

```json
{
    "id": "a1b2c3d4e5f60001",
    "name": "Sim Racing OSC Output",
    "transport": "osc",
    "direction": "output",
    "active": false,
    "osc": {
        "host": "127.0.0.1",
        "sendPort": 9066,
        "recvPort": 9068
    },
    "ws": { "port": 4269 },
    "fields": [
        { "fieldId": "axis_steering",  "oscPath": "/input/steering",  "wsKey": "steering",  "enabled": true },
        { "fieldId": "axis_throttle",  "oscPath": "/input/throttle",  "wsKey": "throttle",  "enabled": true },
        { "fieldId": "axis_brake",     "oscPath": "/input/brake",     "wsKey": "brake",     "enabled": true }
    ]
}
```

### Custom Output Fields

Add your own analog axes or digital buttons to `protocols/input_fields.json`. They will appear in the Protocol Editor field picker alongside the built-in fields.

```json
{
  "output_fields": [
    {
      "id":       "my_custom_axis",
      "label":    "My Custom Axis",
      "category": "Custom",
      "type":     "analog",
      "oscPath":  "/custom/my_axis",
      "wsKey":    "my_custom_axis"
    }
  ]
}
```

Supported `type` values: `"analog"` (float), `"digital"` (bool / 0|1).

Reload the catalog at runtime via **Network Server → Protocols → Reload Fields**.

### Built-in Output Fields (server → client)

**Analog Axes**

| ID | Label |
|---|---|
| `axis_steering` | Steering / Yaw |
| `axis_throttle` | Throttle |
| `axis_clutch` | Clutch |
| `axis_brake` | Brake |
| `axis_handbrake` | Handbrake |
| `axis_pitch` | Pitch |
| `axis_roll` | Roll |
| `axis_collective` | Collective |

**Digital: Vehicle**

`btn_gear_up`, `btn_gear_down`, `btn_neutral`, `btn_reverse`, `btn_gear_1` … `btn_gear_6`, `btn_drive_fwd`, `btn_drive_bwd`, `btn_drive_awd`, `btn_drive_4wd`, `btn_difflock_f`, `btn_difflock_b`

**Digital: Lights**

`btn_lights`, `btn_beam`, `btn_parking`, `btn_fog`, `btn_turn_left`, `btn_turn_right`, `btn_hazard`

**Digital: Other**

`btn_horn`, `btn_cam_switch`, `btn_landing_gear`, `btn_boost`, `btn_jump`, `btn_weapon_main`, `btn_weapon_sec`, `btn_reload`

### Built-in Input Fields (client → server, haptic/rumble)

| ID | Label |
|---|---|
| `haptic_rumble` | Rumble |
| `haptic_constant` | Constant Force |
| `haptic_periodic` | Periodic Effect |
| `haptic_condition` | Condition Effect |
| `haptic_gain` | Global Gain |
| `rumble_left` | Rumble Left Motor |
| `rumble_right` | Rumble Right Motor |

---

## Haptic Message JSON Format

The WebSocket server accepts JSON messages to trigger haptic effects on connected devices.

### General Structure

```json
{
  "device": <integer>,      // SDL Joystick Instance ID
  "type": "<string>",       // "gamepad", "steering_wheel", or "flight_stick"
  "effect": "<string>",     // Effect name (e.g., "rumble", "constant", "condition")
  "params": { ... }
}
```

### 1. Gamepad Rumble

*   **type**: `"gamepad"` · **effect**: `"rumble"`
*   **params**: `large_magnitude` (0.0–1.0), `small_magnitude` (0.0–1.0), `duration_ms`

```json
{ "device": 1, "type": "gamepad", "effect": "rumble",
  "params": { "large_magnitude": 0.8, "small_magnitude": 0.2, "duration_ms": 500 } }
```

### 2. Steering Wheel / Flight Stick: Constant Force

*   **type**: `"steering_wheel"` or `"flight_stick"` · **effect**: `"constant"`
*   **params**: `strength` (-1.0–1.0), `duration_ms`

```json
{ "device": 1, "type": "steering_wheel", "effect": "constant",
  "params": { "strength": 0.5, "duration_ms": 1000 } }
```

### 3. Steering Wheel / Flight Stick: Periodic Effect

*   **type**: `"steering_wheel"` or `"flight_stick"` · **effect**: `"periodic"`
*   **params**:
    *   `wave_type` — Waveform shape:

        | Value | Name | Character |
        |---|---|---|
        | `0` | Sine *(default)* | Smooth sinusoidal oscillation |
        | `1` | Triangle | Linear ramp up then down |
        | `2` | Sawtooth Up | Gradual rise, instant drop |
        | `3` | Sawtooth Down | Instant rise, gradual drop |

    *   `strength` (0.0–1.0) — Overall output gain
    *   `period` (ms) — Duration of one full cycle
    *   `magnitude` (0.0–1.0) — Peak amplitude of the wave
    *   `offset` (-1.0–1.0) — Mean value (centre point) of the wave
    *   `phase` (0–36000) — Starting phase offset in hundredths of a degree
    *   `duration_ms` — Duration in ms; use `-1` for infinite

```json
{ "device": 1, "type": "steering_wheel", "effect": "periodic",
  "params": { "wave_type": 1, "strength": 1.0, "period": 100,
              "magnitude": 0.5, "offset": 0.0, "phase": 0, "duration_ms": 2000 } }
```

### 4. Steering Wheel / Flight Stick: Condition Effect

Position-dependent forces. On flight sticks, the effect is applied on both the pitch and roll axes simultaneously.

*   **type**: `"steering_wheel"` or `"flight_stick"` · **effect**: `"condition"`
*   **params**:
    *   `slot` — Effect slot (0 to device max, default `0`). Multiple slots allow independent effects to run simultaneously.
    *   `condition_type` — Condition type index:

        | Value | Name | Description |
        |---|---|---|
        | `0` | Spring | Restoring force toward centre |
        | `1` | Damper | Resistance proportional to velocity |
        | `2` | Inertia | Resistance proportional to acceleration |
        | `3` | Friction | Constant resistance regardless of speed |

        Defaults to `0` (Spring).
    *   `right_sat` (0.0–1.0) — Saturation on the positive side
    *   `left_sat` (0.0–1.0) — Saturation on the negative side
    *   `right_coeff` (-1.0–1.0) — Slope on the positive side
    *   `left_coeff` (-1.0–1.0) — Slope on the negative side
    *   `deadband` (0.0–1.0) — Range around centre with no force
    *   `center` (-1.0–1.0) — Centre point of the effect
    *   `duration_ms` — Duration in ms; use `-1` for infinite

```json
{ "device": 1, "type": "steering_wheel", "effect": "condition",
  "params": { "slot": 0, "condition_type": 0,
              "right_sat": 1.0, "left_sat": 1.0,
              "right_coeff": 0.5, "left_coeff": 0.5,
              "deadband": 0.1, "center": 0.0, "duration_ms": -1 } }
```

> **Migration note:** Prior to this change, `condition_type` used SDL's internal bitmask values (`128`=Spring, `256`=Damper, `512`=Inertia, `1024`=Friction). It now uses the simple 0–3 index shown above. Any existing integrations must be updated.

---

## OSC Format and Messages 🔊

InputBridge also supports sending and receiving Open Sound Control (OSC) messages for haptics control and wheel telemetry.

- **Default send host:** `127.0.0.1`
- **Default send port:** `9066`
- **Default receive port:** `9068`

### Incoming (Control) Messages

InputBridge listens on two separate sets of paths depending on which protocol handler is active.

**`/inputbridge/haptics/*`** — Used by the OSCProtocol / OSCBaseProtocol handler (selected via the Protocol Editor). The first argument is always the device ID (`i`); the server currently uses the UI-selected device (the ID is still required in the message signature).

**`/haptic/*`** — Shorter paths registered directly by OSCServer as dedicated handlers. Same argument layout as their `/inputbridge/haptics/*` equivalents.

All type tags use liblo notation: `i` = int32, `f` = float32. For duration arguments, send `-1` to play indefinitely (`SDL_HAPTIC_INFINITY`).

---

#### Rumble

Triggers low- and high-frequency motor vibration (gamepads and rumble-capable devices).

| Path | Types | Arguments |
|---|---|---|
| `/inputbridge/haptics/rumble` | `iiffi` | deviceId, slot, low_freq (0.0–1.0), high_freq (0.0–1.0), duration_ms |
| `/haptic/rumble` | `iiffi` | deviceId, slot, low_freq (0.0–1.0), high_freq (0.0–1.0), duration_ms |

Example: `[1, 0, 0.8, 0.2, 500]`

---

#### Constant Force

Applies a steady directional force (steering wheels and flight sticks).

| Path | Types | Arguments |
|---|---|---|
| `/inputbridge/haptics/force` | `iifi` | deviceId, slot, strength (-1.0–1.0), duration_ms |
| `/haptic/constant` | `iifi` | deviceId, slot, strength (-1.0–1.0), duration_ms |

Example: `[1, 0, 0.5, 1000]`

---

#### Periodic Effect

Applies a repeating wave effect (engine vibration, road texture, etc.). The wave shape is selected with `wave_type`: `0`=Sine (default), `1`=Triangle, `2`=Sawtooth Up, `3`=Sawtooth Down.

| Path | Types | Arguments |
|---|---|---|
| `/inputbridge/haptics/periodic` | `iiififfii` | deviceId, slot, wave_type, strength (0.0–1.0), period (ms), magnitude (0.0–1.0), offset (-1.0–1.0), phase (0–36000), duration_ms |
| `/haptic/periodic` | `iiififfii` | deviceId, slot, wave_type, strength (0.0–1.0), period (ms), magnitude (0.0–1.0), offset (-1.0–1.0), phase (0–36000), duration_ms |

Example (triangle wave, 150 ms period, 2 seconds): `[1, 0, 1, 1.0, 150, 0.5, 0.0, 0, 2000]`

> **Backward compatibility:** The legacy 8-argument format `iififfii` (without `wave_type`) is still accepted and defaults to Sine.

---

#### Condition Effect

Position-dependent forces. On flight sticks the effect is applied on both pitch and roll axes.

| Path | Types | Arguments |
|---|---|---|
| `/inputbridge/haptics/condition` | `iiiffffffi` | deviceId, slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms |
| `/haptic/condition` | `iiiffffffi` | deviceId, slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms |

`condition_type`: `0`=Spring, `1`=Damper, `2`=Inertia, `3`=Friction

Ranges: `right_sat` / `left_sat` 0.0–1.0 · `right_coeff` / `left_coeff` −1.0–1.0 · `deadband` 0.0–1.0 · `center` −1.0–1.0

Example (spring, slot 0, infinite): `[1, 0, 0, 1.0, 1.0, 0.5, 0.5, 0.1, 0.0, -1]`

---

#### Global Gain

Sets the overall haptic output gain for a device (0–100).

| Path | Types | Arguments |
|---|---|---|
| `/inputbridge/haptics/gain` | `ii` | deviceId, gain (0–100) |
| `/haptic/gain` | `ii` | deviceId, gain (0–100) |

Example: `[1, 80]`

---

#### DualSense Adaptive Triggers

Controls Sony DualSense adaptive trigger effects on PS5 controllers. All paths begin with `/inputbridge/haptics/dualsense/trigger/{side}/` where `{side}` is `left` or `right`.

| Path | Types | Arguments | Description |
|---|---|---|---|
| `/inputbridge/haptics/dualsense/trigger/{side}/feedback` | `iii` | deviceId, position (0–9), strength (0–8) | Resistance starting at `position` |
| `/inputbridge/haptics/dualsense/trigger/{side}/weapon` | `iiii` | deviceId, start_position (2–7), end_position (start+1–8), strength (0–8) | Weapon click effect |
| `/inputbridge/haptics/dualsense/trigger/{side}/vibration` | `iiii` | deviceId, position (0–9), amplitude (0–8), frequency (0–255) | Vibration effect |
| `/inputbridge/haptics/dualsense/trigger/{side}/off` | `i` | deviceId | Disable adaptive trigger |

Examples:
```
# Feedback on right trigger, resistance from position 4 at strength 6
[1, 4, 6]  →  /inputbridge/haptics/dualsense/trigger/right/feedback

# Weapon click on left trigger between positions 3 and 7, strength 8
[1, 3, 7, 8]  →  /inputbridge/haptics/dualsense/trigger/left/weapon

# Vibration on right trigger at position 2, amplitude 5, frequency 120 Hz
[1, 2, 5, 120]  →  /inputbridge/haptics/dualsense/trigger/right/vibration

# Turn off left trigger effect
[1]  →  /inputbridge/haptics/dualsense/trigger/left/off
```

---

#### RPM LED Bar

Sets the RPM LED strip brightness on all connected RPM-capable steering wheels (Fanatec, Logitech, Thrustmaster). Value is normalised 0.0–1.0.

| Path | Types | Arguments |
|---|---|---|
| `/inputbridge/wheel/led_rpm` | `f` | rpm_percent (0.0–1.0) |

Example: `[0.75]` — set LEDs to 75 % of the bar

---

### Outgoing (Telemetry) Messages

InputBridge sends wheel telemetry under `/wheel/*` and button states under `/wheel/buttons/*`.

| Path | Type | Description |
|---|---|---|
| `/wheel/steer` | `f` | Steering axis |
| `/wheel/brake` | `f` | Brake axis |
| `/wheel/throttle` | `f` | Throttle axis |
| `/wheel/pitch` | `f` | Pitch axis |
| `/wheel/roll` | `f` | Roll axis |
| `/wheel/buttons/0` … `/wheel/buttons/3` | `i` | Button states |

---

## Additional Features

### Backup Manager

InputBridge automatically creates timestamped backups of protocol definitions before destructive operations (e.g., delete, overwrite). Backups are stored in a `backups/` directory next to the executable. Up to 10 backups are retained per file; older ones are cleaned up automatically.

### Undo / Redo

The Protocol Editor supports up to 50 levels of undo/redo for all editing operations. Use **Ctrl+Z** / **Ctrl+Y** (or the Edit menu) to step through history.

### Protocol Validation

When importing a protocol definition file, InputBridge validates its JSON structure, field IDs, OSC paths, WebSocket keys, host addresses, and port numbers before applying any changes, reporting errors and warnings clearly.

### RPM LEDs

For supported steering wheels (Fanatec, Logitech, Thrustmaster), InputBridge can drive the RPM LED strip directly via the **RPM LEDs** tab in the device panel. Devices are detected automatically when a wheel is connected.

---

## Building

### Prerequisites

- CMake 3.16+
- C++20 compiler
- SDL3 (source in `lib/sdl/` or installed system-wide)

### CMake Options

| Option | Default | Description |
|---|---|---|
| `ENABLE_EXCLUSIVE_INPUT` | `ON` | Device hide support (HidHide / evdev grab / IOKit seize) |
| `ENABLE_WEBSOCKETS` | `ON` | WebSocket server support |
| `ENABLE_OSC` | `ON` | OSC server support |

### Windows

Requires [HidHide](https://github.com/nefarius/HidHide) to be installed at runtime for the device-hide feature to function. The build itself has no HidHide SDK dependency — communication uses only `setupapi`, `cfgmgr32`, and standard Win32 `DeviceIoControl`. Links: `setupapi`, `cfgmgr32`, `ws2_32`, `comdlg32`.

### Linux

The device-hide feature uses `EVIOCGRAB` (evdev), available on all modern Linux kernels. No extra packages required. The process must have read/write access to `/dev/input/event*` nodes — adding the user to the `input` group is typically sufficient.

### macOS

The device-hide feature uses `IOHIDOptionsTypeSeizeDevice` from the IOKit framework, which is linked automatically when `ENABLE_EXCLUSIVE_INPUT=ON`.