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
