# InputBridge

[![Build Status](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml) [![Test Results (Ubuntu)](https://github.com/marsmaantje/InputBridge/workflows/Test%20Results%20(ubuntu-latest)/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml) [![Test Results (Windows)](https://github.com/marsmaantje/InputBridge/workflows/Test%20Results%20(windows-latest)/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml) [![Test Results (macOS)](https://github.com/marsmaantje/InputBridge/workflows/Test%20Results%20(macos-latest)/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml)

InputBridge is a cross-platform input device bridge that relays joystick, gamepad, steering wheel, and other controller data over OSC and WebSocket. It supports haptic feedback control, adaptive trigger effects, and a fully configurable protocol system.

---

## Supported Devices

| Device Type | Features |
|---|---|
| Generic Gamepad | Rumble (low/high frequency motors) |
| Xbox One / Series X\|S | Rumble + impulse trigger motors (left/right independently) |
| Sony DualSense (PS5) | Adaptive triggers (7 effect types), rumble, LED control, player indicators — USB & Bluetooth |
| Steering Wheel | Constant force, periodic effects, condition effects |
| Flight Stick | Axis + button visualisation |
| Wiimote | Button + axis visualisation |

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

All messages must be a JSON object containing the target device ID, the device type, the effect name, and a parameters object.

```json
{
  "device": <integer>,      // SDL Joystick Instance ID
  "type": "<string>",       // "gamepad" or "steering_wheel"
  "effect": "<string>",     // Effect name (e.g., "rumble", "constant")
  "params": {               // Effect-specific parameters
    ...
  }
}
```

### 1. Gamepad Rumble
Used for standard controller vibration.

*   **type**: `"gamepad"`
*   **effect**: `"rumble"`
*   **params**:
    *   `large_magnitude`: Float (0.0 to 1.0) - Low frequency motor intensity.
    *   `small_magnitude`: Float (0.0 to 1.0) - High frequency motor intensity.
    *   `duration_ms`: Integer - Duration in milliseconds.

**Example:**
```json
{
  "device": 1,
  "type": "gamepad",
  "effect": "rumble",
  "params": {
    "large_magnitude": 0.8,
    "small_magnitude": 0.2,
    "duration_ms": 500
  }
}
```

### 2. Steering Wheel: Constant Force
Used for constant resistance or force feedback.

*   **type**: `"steering_wheel"`
*   **effect**: `"constant"`
*   **params**:
    *   `strength`: Float (-1.0 to 1.0) - Force level.
    *   `duration_ms`: Integer - Duration in milliseconds.

**Example:**
```json
{
  "device": 1,
  "type": "steering_wheel",
  "effect": "constant",
  "params": {
    "strength": 0.5,
    "duration_ms": 1000
  }
}
```

### 3. Steering Wheel: Periodic Effect
Used for sine waves or vibration textures (e.g., engine vibration, road surface).

*   **type**: `"steering_wheel"`
*   **effect**: `"periodic"`
*   **params**:
    *   `strength`: Float (0.0 to 1.0) - Overall gain/strength.
    *   `period`: Integer - Period of the wave in milliseconds.
    *   `magnitude`: Float (0.0 to 1.0) - Peak magnitude of the wave.
    *   `offset`: Float (-1.0 to 1.0) - Mean value of the wave.
    *   `phase`: Integer - Phase shift (0 to 36000, representing 0.00 to 360.00 degrees).
    *   `duration_ms`: Integer - Duration in milliseconds.

**Example:**
```json
{
  "device": 1,
  "type": "steering_wheel",
  "effect": "periodic",
  "params": {
    "strength": 1.0,
    "period": 100,
    "magnitude": 0.5,
    "offset": 0.0,
    "phase": 0,
    "duration_ms": 2000
  }
}
```

### 4. Steering Wheel: Condition Effect
Used for spring, damper, friction, or inertia effects (e.g., centering spring).

*   **type**: `"steering_wheel"`
*   **effect**: `"condition"`
*   **params**:
    *   `slot`: Integer - The effect slot to use (0 to device max). Defaults to 0.
    *   `condition_type`: Integer - The haptic condition type ID.
        *   `128` (Spring), `256` (Damper), `512` (Inertia), `1024` (Friction).
        *   Defaults to 128 (Spring).
    *   `right_sat`: Float (0.0 to 1.0) - Saturation level on the positive side.
    *   `left_sat`: Float (0.0 to 1.0) - Saturation level on the negative side.
    *   `right_coeff`: Float (-1.0 to 1.0) - Coefficient (slope) on the positive side.
    *   `left_coeff`: Float (-1.0 to 1.0) - Coefficient (slope) on the negative side.
    *   `deadband`: Float (0.0 to 1.0) - Range around center where no force is applied.
    *   `center`: Float (-1.0 to 1.0) - Center point for the effect.
    *   `duration_ms`: Integer - Duration in milliseconds.

**Example:**
```json
{
  "device": 1,
  "type": "steering_wheel",
  "effect": "condition",
  "params": {
    "slot": 0,
    "condition_type": 128,
    "right_sat": 1.0,
    "left_sat": 1.0,
    "right_coeff": 0.5,
    "left_coeff": 0.5,
    "deadband": 0.1,
    "center": 0.0,
    "duration_ms": 5000
  }
}
```

## OSC Format and Messages 🔊

InputBridge also supports sending and receiving Open Sound Control (OSC) messages for haptics control and wheel telemetry.

- **Default send host:** `127.0.0.1`
- **Default send port:** `9066`
- **Default receive port:** `9068`

### Incoming (Control) Messages

All incoming haptics messages are under the `/inputbridge/haptics/*` namespace. Types use liblo-style type tags where `i` = int and `f` = float. The first argument is reserved for a device id (int), but the server currently uses the *selected* device from the UI (the id is still expected to match the signature).

- `/inputbridge/haptics/rumble` — types: "iffi" (deviceId, low_freq, high_freq, duration_ms)
  - Example arguments: `[1, 0.8, 0.2, 500]` — Rumble with low/high magnitude and duration.

- `/inputbridge/haptics/force` — types: "ifi" (deviceId, strength, duration_ms)
  - Example: `[1, 0.5, 1000]` — Constant force on steering wheels.

- `/inputbridge/haptics/periodic` — types: "ififfii" (deviceId, strength, period, magnitude, offset, phase, duration_ms)
  - Example: `[1, 1.0, 100, 0.5, 0.0, 0, 2000]`

- `/inputbridge/haptics/condition` — types: "iiiffffffi" (deviceId, slot, type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms).
  - `slot`: Integer - The effect slot to use (0 to device max).
  - `type`: Integer - The haptic condition type ID: `128` (Spring), `256` (Damper), `512` (Inertia), `1024` (Friction).
  - Example: `[1, 0, 128, 1.0, 1.0, 0.5, 0.5, 0.1, 0.0, 5000]`

- `/inputbridge/haptics/gain` — types: "ii" (deviceId, gain)
  - Example: `[1, 100]`

> Note: the device ID argument is currently ignored by the handlers in favor of the UI-selected device ID, but the message signature still requires the ID value to be present.

### Outgoing (Telemetry) Messages

InputBridge sends wheel telemetry under `/wheel/*` and button states under `/wheel/buttons/*`.

- `/wheel/steer` — type: `f` (float)
- `/wheel/brake` — type: `f`
- `/wheel/throttle` — type: `f`
- `/wheel/pitch` — type: `f`
- `/wheel/roll` — type: `f`
- `/wheel/buttons/0`...`/wheel/buttons/3` — type: `i` (int)

These messages are emitted using the configured protocol/version (selectable in the UI) and are useful for external telemetry or integrations.

---

## Additional Features

### Backup Manager

InputBridge automatically creates timestamped backups of protocol definitions before destructive operations (e.g., delete, overwrite). Backups are stored in a `backups/` directory next to the executable. Up to 10 backups are retained per file by default; older ones are cleaned up automatically.

### Undo / Redo

The Protocol Editor supports up to 50 levels of undo/redo for all editing operations. Use **Ctrl+Z** / **Ctrl+Y** (or the Edit menu) to step through history.

### Protocol Validation

When importing a protocol definition file, InputBridge validates its JSON structure, field IDs, OSC paths, WebSocket keys, host addresses, and port numbers before applying any changes, reporting errors and warnings clearly.
