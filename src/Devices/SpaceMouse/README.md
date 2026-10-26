# 3Dconnexion (SpaceMouse / SpaceNavigator) support

Reads 3Dconnexion 6-DOF devices over raw HID and exposes each one as a
virtual SDL joystick, so it is mappable like any other device (same pattern
as `Devices/Wiimote/`). Report layout follows
[OpenNDOF](https://github.com/bpmcgill/OpenNDOF).

## Not interfering with existing drivers

| Rule | How it is enforced |
| --- | --- |
| Never write to the device | No output/feature report is sent anywhere (no LEDs, mode switches, LCD). `SpaceMouseDevice` has no write path. |
| Never open exclusively | Opened only through `SDL_hid_open_path` (shared). No HidHide, evdev grab or IOKit seize is applied to these devices. The bridge joysticks are virtual, so the "Hide from other applications" option cannot act on the real hardware. |
| Touch only 3Dconnexion hardware | Enumeration is limited to VID `256F` and the known `046D` PIDs, and to the Multi-axis Controller HID interface. Other Logitech devices are never opened. |
| Fail soft | If another driver holds the device exclusively, the open fails, is logged once, and is retried quietly. |
| Kill switch | Settings -> "3Dconnexion Device Support" releases every handle. |

3DxWare, spacenavd and CAD applications keep receiving their own copy of every
report (Windows HID per-handle queues, Linux hidraw per-open readers).

## Layout of the bridge joystick

Axes (append-only, bindings persist by raw index):
`0..5` = TX TY TZ RX RY RZ (raw device sign, normalised so +-350 = +-1),
`6` = battery (wireless models; -1 when unknown).
Buttons: one per physical button, count depends on the model.

## Linux

Install `packaging/linux/udev/71-inputbridge-3dconnexion.rules` (copy to
`/etc/udev/rules.d/`, reload, replug). The in-app "Fix permissions" button
only handles the Wii rules.

## Known limits

* Model PIDs and button counts come from public sources and are unverified
  against hardware; unknown 3Dconnexion-vendor multi-axis devices fall back
  to a generic 32-button entry.
* Combined 13-byte motion reports and battery report `0x17` are handled
  defensively but untested on real devices.
* SpacePilot LCD and device LEDs are intentionally not supported (they would
  require writing to the device).
