# InputBridge

[![Cross-platform building test](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml/badge.svg)](https://github.com/marsmaantje/InputBridge/actions/workflows/build.yml)

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

- `/inputbridge/haptics/condition` — types: "iffffffi" (deviceId, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms)
  - Example: `[1, 1.0, 1.0, 0.5, 0.5, 0.1, 0.0, 5000]`

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

If you'd like, I can also add example scripts for sending these messages from the command line or other OSC tools.
