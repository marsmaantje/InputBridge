# Wiimote parity module

Gives InputBridge WiimoteLib-equivalent access to Wii Remote / Wii Remote
Plus / Wii Balance Board / Nunchuk / Classic Controller / Guitar Hero
guitars & drums, by talking raw HID directly (`SDL_hid_*`) instead of going
through `SDL_Joystick`. See the chat thread that produced this for why: SDL's
own Wii driver only exposes buttons/accel/rumble because that's all
`SDL_Gamepad` has room to model.

Drives the device itself end-to-end (handshake, extension detection,
register read/write, report decoding) rather than layering on top of
SDL_Joystick, the same way you'd write any raw-HID device backend: open the
handle with `SDL_hid_open_path`, poll it non-blockingly each frame, and keep
your own decoded state separate from anything SDL owns.

## Files

| File | Purpose |
| --- | --- |
| `WiimoteProtocol.h` | Report IDs, register addresses, extension IDs, IR sensitivity blocks. Numeric constants only, verified against wiibrew.org (2026-08-15). |
| `WiimoteState.h` | Decoded-value structs (`CoreButtons`, `AccelState`, `IRState`, `NunchukState`, `ClassicControllerState`, `GuitarHeroState`, `BalanceBoardState`). |
| `WiimoteDecoder.h/.cpp` | **Pure** byte→struct decode functions, no I/O. Unit tested in `tests/test_wiimote_decoder.cpp`. |
| `WiimoteDevice.h/.cpp` | Owns one `SDL_hid_device*`. Handshake, polling, register read/write, LEDs/rumble. |
| `WiimoteManager.h/.cpp` | Enumerates & opens all connected Wiimotes/Balance Boards. |

## Feature parity vs. WiimoteLib

| Feature | WiimoteLib | This module |
| --- | :-: | :-: |
| Buttons | ✅ | ✅ |
| Accelerometer | ✅ | ✅ (nominal 0g/1g; see `AccelState` comment for calibrated version) |
| IR camera (4-point) | ✅ | ✅ basic mode (X/Y only). Extended/Full mode (object size, bounding box) not wired up - `IRMode::Extended/Full` constants exist, decoder does not yet. |
| Nunchuk | ✅ | ✅ |
| Classic Controller (+ Pro) | ✅ | ✅ |
| Guitar Hero Guitar/Drums | ✅ | ⚠️ Frets/strum/whammy/joystick decoded via the Classic-Controller-shaped byte layout, which is how existing OSS drivers do it - **not verified against real hardware**. Drum pad velocities not implemented. |
| Wii Balance Board (4 sensors + weight) | ✅ | ✅ full 3-point calibrated kg conversion, per-sensor + total weight, center-of-gravity |
| LEDs | ✅ | ✅ |
| Rumble | ✅ | ✅ |
| Battery | ✅ | ✅ |
| Speaker | ✅ | ❌ not implemented (report 0x14/0x18/0x19 + register config documented in `WiimoteProtocol.h`) |
| Raw register read/write | ✅ | ✅ (`ReadRegister`/`WriteRegister`, public) |
| Multiple Wiimotes | ✅ | ✅ (`WiimoteManager::Scan()` returns a vector) |
| Wii Motion Plus | ❌ (predates it) | ❌ not implemented; `ExtensionType::MotionPlus` is detected but not decoded |

## Wiring into InputBridge

This is scaffolding, not a finished PR - three integration points remain:

1. **Coexistence with SDL's own Wii driver.** Right now `Application.cpp`
   sets `SDL_HINT_JOYSTICK_HIDAPI_WII=1`, so SDL's HIDAPI backend will race
   this module for the same HID handle. Simplest fix: set that hint to `"0"`
   and let `WiimoteManager` be the only thing that ever opens a Wiimote.
   You lose nothing - this module's `WiimoteSnapshot` already carries
   everything the old SDL-backed `DeviceState` did (buttons, accel) plus
   everything it didn't.

2. **DeviceManager plumbing.** Add a `std::vector<std::unique_ptr<WiimoteDevice>> m_Wiimotes`
   next to `m_WheelRPMDevices`, a `ScanWiimotes()` alongside
   `ScanWheelRPMDevices()`, call `dev->Poll()` for each in `Update()`, and
   expose `GetWiimotes()`. Re-scan periodically (every few seconds) to catch
   devices paired while the app is running - the same rationale applies to
   any Bluetooth device that doesn't route through SDL's joystick hotplug
   events.

3. **Protocol fields + visualizer.** Add field IDs to
   `protocols/input_fields.json` (`wiimote_ir_x1..4`/`y1..4`,
   `balance_kg_tl/tr/bl/br`, `balance_kg_total`, `balance_cog_x/y`,
   `guitar_fret_green` etc.) and a resolver in `ProtocolFieldUtils` that
   reads from a `WiimoteSnapshot` the same way it currently reads gyro/accel
   from `SensorState`. Extend `WiimoteVisualizer` to show IR dots on a
   camera-FOV rectangle and a 4-corner weight readout for Balance Boards
   (`DevicePanel.cpp` already special-cases the tab by name match on
   `"Wiimote"` - extend that check to also route Balance Board devices here
   or to a new `BalanceBoardVisualizer`).

## CMake

Add to `CMakeLists.txt`, alongside the other `src/Devices/` sources:

```cmake
src/Devices/Wiimote/WiimoteDecoder.cpp
src/Devices/Wiimote/WiimoteDevice.cpp
src/Devices/Wiimote/WiimoteManager.cpp
```

And to `tests/CMakeLists.txt`:

```cmake
tests/test_wiimote_decoder.cpp
```

(`WiimoteDecoder.cpp` has no SDL dependency, so it links straight into the
test binary without pulling in real HID I/O.)

## Known gaps / before you ship this

- **IR init timing is unverified on real hardware.** WiiBrew notes the
  camera can land in a random state (on/off/half-sensitivity/full) and
  recommends 50ms between each register write during init, repeating until
  the right state is observed. `WiimoteDevice::EnableIRCamera()` currently
  fires the writes back-to-back with no delay/retry loop - add one if dots
  don't show up reliably in testing.
- **Extension encryption.** This uses the documented "new way" init (write
  `0x55`→`0xA400F0`, `0x00`→`0xA400FB`), which leaves data unencrypted on all
  known official and most third-party extensions. Some wireless/third-party
  Nunchuks need the encrypted path instead (see WiiBrew's Nunchuk page,
  "Wireless Nunchuks" section) - not implemented here.
- **Balance Board calibration CRC32 is not verified** (see comment in
  `WiimoteDecoder.h`) - a corrupted read would silently use bad calibration
  until the next successful read.
- **`ReadRegister()`'s inline report-draining** (in the "wait for 0x21"
  loop) only refreshes buttons for reports it swallows during a register
  read; accel/IR/extension updates are briefly stalled during any read
  (~ms-scale, only matters for very read-heavy code paths like the initial
  handshake).
- No tests exist yet for `WiimoteDevice`/`WiimoteManager` themselves (they
  need a fake `SDL_hid_device*` / injectable transport to be testable
  without hardware - e.g. a thin interface wrapping `SDL_hid_write`/
  `SDL_hid_read` that a test can substitute with canned byte sequences).
