# InputBridge – Protocol Definitions

This folder is created automatically next to the application executable.

## Structure

```
protocols/
├── input_fields.json          ← External field catalog (editable)
├── definitions/               ← One .json file per protocol definition
│   ├── <id1>.json
│   ├── <id2>.json
│   └── ...
└── README.md                  ← This file
```

---

## `input_fields.json` — Custom Output Fields

Add your own analog axes or digital buttons here. They will appear in the
Protocol Editor field picker alongside the built-in fields.

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

---

## `definitions/<id>.json` — Protocol Definition Files

These are managed by the Protocol Editor UI. Each file represents one
named protocol configuration.

### Example – OSC Output (sends game input data to a sim client)

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
        { "fieldId": "axis_brake",     "oscPath": "/input/brake",     "wsKey": "brake",     "enabled": true },
        { "fieldId": "btn_gear_up",    "oscPath": "/input/gear_up",   "wsKey": "gear_up",   "enabled": true },
        { "fieldId": "btn_gear_down",  "oscPath": "/input/gear_down", "wsKey": "gear_down", "enabled": true }
    ]
}
```

### Example – WebSocket Input (receives haptic commands from a client)

```json
{
    "id": "a1b2c3d4e5f60002",
    "name": "Haptic WebSocket Input",
    "transport": "websocket",
    "direction": "input",
    "active": false,
    "osc": { "host": "127.0.0.1", "sendPort": 9066, "recvPort": 9068 },
    "ws": { "port": 4270 },
    "fields": [
        { "fieldId": "haptic_rumble_low",  "oscPath": "/haptic/rumble_low",  "wsKey": "rumble_low",  "enabled": true },
        { "fieldId": "haptic_rumble_high", "oscPath": "/haptic/rumble_high", "wsKey": "rumble_high", "enabled": true },
        { "fieldId": "haptic_const_force", "oscPath": "/haptic/const_force", "wsKey": "const_force", "enabled": true }
    ]
}
```

---

## Built-in Output Fields (sent server → client)

### Analog Axes
| ID | Label |
|----|-------|
| `axis_steering`   | Steering / Yaw |
| `axis_throttle`   | Throttle |
| `axis_clutch`     | Clutch |
| `axis_brake`      | Brake |
| `axis_handbrake`  | Handbrake |
| `axis_pitch`      | Pitch |
| `axis_roll`       | Roll |
| `axis_collective` | Collective |

### Digital: Vehicle
`btn_gear_up`, `btn_gear_down`, `btn_neutral`, `btn_reverse`,
`btn_gear_1` … `btn_gear_6`, `btn_drive_fwd`, `btn_drive_bwd`,
`btn_drive_awd`, `btn_drive_4wd`, `btn_difflock_f`, `btn_difflock_b`

### Digital: Lights
`btn_lights`, `btn_beam`, `btn_parking`, `btn_fog`,
`btn_turn_left`, `btn_turn_right`, `btn_hazard`

### Digital: Other
`btn_horn`, `btn_cam_switch`, `btn_landing_gear`, `btn_boost`,
`btn_jump`, `btn_weapon_main`, `btn_weapon_sec`, `btn_reload`

---

## Built-in Input Fields (received client → server, haptic / rumble)

| ID | Label |
|----|-------|
| `haptic_rumble_low`  | Rumble – Low Frequency |
| `haptic_rumble_high` | Rumble – High Frequency |
| `haptic_rumble_dur`  | Rumble – Duration (ms) |
| `haptic_const_force` | Constant Force |
| `haptic_const_dur`   | Constant Force Duration |
| `haptic_spring`      | Spring / Centering |
| `haptic_damper`      | Damper |
| `haptic_friction`    | Friction |
| `rumble_left`        | Rumble Left Motor |
| `rumble_right`       | Rumble Right Motor |
