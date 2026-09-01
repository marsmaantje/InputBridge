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
| `WiimoteDevice.h/.cpp` | Owns one `SDL_hid_device*`. Handshake, polling, register read/write, LEDs/rumble. `InitExtension()` tries the documented "new way" init (write `0x55`→`0xA400F0`, `0x00`→`0xA400FB`, data unencrypted) first, then falls back to the "old way" (write `0x00`→`0xA400F0` only, data encrypted, decrypted per `WiimoteProtocol.h::DecryptExtensionByte()`) for wireless/third-party Nunchuks that need it - see WiiBrew's Nunchuk page, "Wireless Nunchuks" section. `WiimoteSnapshot::extension_encrypted` reports which path is active. |
| `WiimoteManager.h/.cpp` | Enumerates & opens all connected Wiimotes/Balance Boards. |

Bluetooth *pairing* (as opposed to enumerating/opening already-paired
devices) is a separate module, `src/Bluetooth/` - see the "Bluetooth
pairing" section below.

## Feature parity vs. WiimoteLib

| Feature | WiimoteLib | This module |
| --- | :-: | :-: |
| Buttons | ✅ | ✅ |
| Accelerometer | ✅ | ✅ (nominal 0g/1g; see `AccelState` comment for calibrated version) |
| IR camera (4-point) | ✅ | ✅ basic mode (X/Y only) and extended mode (adds 4-bit dot size) via `WiimoteDevice::SetIRExtendedMode()`. Full mode (bounding box + intensity, needs the interleaved 0x3e/0x3f report pair) still not wired up - `IRMode::Full` constant exists, decoder does not yet. |
| Nunchuk | ✅ | ✅ |
| Classic Controller (+ Pro) | ✅ | ✅ |
| Guitar Hero Guitar/Drums | ✅ | ⚠️ Frets/strum/whammy/joystick decoded via the Classic-Controller-shaped byte layout, which is how existing OSS drivers do it - **not verified against real hardware**. Drum pad velocities not implemented. |
| Wii Balance Board (4 sensors + weight) | ✅ | ✅ full 3-point calibrated kg conversion, per-sensor + total weight, center-of-gravity |
| LEDs | ✅ | ✅ |
| Rumble | ✅ | ✅ |
| Battery | ✅ | ✅ |
| Speaker | ✅ | ⚠️ 8-bit signed PCM (`EnableSpeaker()`/`QueuePCM8()`) confirmed working on real hardware - default volume tuned down (~25%, `0x40`) after max volume (`0xFF`) was confirmed to distort; `TickSpeaker()` catches up on multiple chunks per `Poll()` call after an earlier version under-delivered data whenever `Poll()`'s cadence was slower than the audio rate, which produced audible crackling. Volume 0 is enforced in software (no data transmitted) rather than trusted to the hardware gain register, which was confirmed on real hardware to still output sound at `VV=0x00`. 4-bit Yamaha ADPCM (`EnableSpeaker(..., SpeakerAudioFormat::ADPCM4)`/`QueueADPCM4()`, encoder in `WiimoteADPCM.h`/`.cpp`) is now the default for `PlayBeep()` and roughly doubles the usable sample rate for the same data rate, which is what actually fixes the thin/aliased sound 8-bit PCM has at the low rates it's limited to - **not independently verified against real hardware yet** (unlike the 8-bit path above); the encoder's own round-trip math is unit-tested (`tests/test_wiimote_adpcm.cpp`), but the nibble-packing order sent over the wire (see `WiimoteADPCM.h`'s header comment) is only known to match other open-source Wiimote drivers' convention, not something WiiBrew states explicitly. If real hardware plays ADPCM4 audio that's present but garbled/pitch-shifted rather than silent, try swapping that nibble order first. |
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

- **In-app Bluetooth pairing (`src/Bluetooth/`) is unverified on all three
  platforms.** It's written against each OS's documented Bluetooth API
  (BlueZ D-Bus, WinRT `Windows.Devices.Enumeration`/`.Bluetooth`,
  IOBluetooth) but hasn't been build- or hardware-tested as part of adding
  it - see the header comment on each platform backend
  (`LinuxWiimoteBluetoothPairing.h`, `WindowsWiimoteBluetoothPairing.h`,
  `MacOSWiimoteBluetoothPairing.h`) for the specific caveats on that
  platform before relying on it. Two specifics worth calling out: the
  Windows AQS filter GUID was checked against Microsoft's own docs and is
  correct; the Linux legacy-PIN fallback (`GetReversedAdapterAddressPin()`
  in `LinuxWiimoteBluetoothPairing.cpp`) may be entirely redundant with
  BlueZ's own built-in Wiimote-PIN handling, or may never even fire under
  a `NoInputNoOutput` agent - see that function's comment for what's
  actually confirmed vs. still open. `WiimoteManager::Scan()` itself is
  unaffected either way - it only enumerates devices the OS already
  considers paired, which is exactly what a successful pairing produces.
- **`ReadRegister()`'s inline report-draining** (in the "wait for 0x21"
  loop) only refreshes buttons for reports it swallows during a register
  read; accel/IR/extension updates are briefly stalled during any read
  (~ms-scale, only matters for very read-heavy code paths like the initial
  handshake).
- No tests exist yet for `WiimoteDevice`/`WiimoteManager` themselves (they
  need a fake `SDL_hid_device*` / injectable transport to be testable
  without hardware - e.g. a thin interface wrapping `SDL_hid_write`/
  `SDL_hid_read` that a test can substitute with canned byte sequences).

## Bluetooth pairing

`src/Bluetooth/WiimoteBluetoothPairing.h` + one platform backend per OS
(`Linux/Windows/MacOSWiimoteBluetoothPairing.*`) let InputBridge discover
and pair a Wiimote itself - press sync, click Scan, click Pair - instead of
requiring the person to go pair it via the OS's own Bluetooth settings
first. It's a separate concern from everything else in this directory:
once a Wiimote is paired (by this module or the old manual way), it's
indistinguishable to `WiimoteManager::Scan()`, which only ever enumerates
already-paired HID devices and doesn't know or care how they got paired.

The UI entry point is `src/UI/WiimotePairingWindow.*` ("Pair Wiimote..."
button in the Devices sidebar section). See
`WiimoteBluetoothPairing.h`'s header comment for the Just Works /
sync-button/20-second-window background a caller needs, and each platform
backend's own header for that platform's specific mechanism and caveats.

Building it needs, per platform:
- **Linux**: `libdbus-1-dev` (Debian/Ubuntu) / `dbus-devel` (Fedora) at
  build time, and `bluetoothd` (BlueZ) reachable on the system bus at
  runtime - both essentially universal on desktop Linux already, since
  they're what every other Bluetooth-pairing UI (GNOME/KDE settings,
  `bluetoothctl`) also depends on. `CMakeLists.txt` checks for the dev
  package via pkg-config and cleanly falls back to the dummy backend
  (dialog reports "not available") if it's missing, rather than failing
  the whole build.
- **Windows**: a Windows SDK new enough to ship the prebuilt `<winrt/...>`
  headers for `Windows.Devices.Enumeration`/`.Bluetooth` (10.0.17134+ in
  practice); links `windowsapp.lib`/`runtimeobject.lib`.
- **macOS**: the `IOBluetooth` framework (part of the standard macOS SDK);
  add `NSBluetoothAlwaysUsageDescription` to the app bundle's Info.plist
  or the permission prompt IOBluetooth needs won't appear and discovery
  will silently find nothing - see `MacOSWiimoteBluetoothPairing.h`.

Set `-DENABLE_WIIMOTE_BLUETOOTH_PAIRING=OFF` to drop the whole feature
(dummy backend everywhere, dialog reports "not available") - mirrors the
existing `ENABLE_EXCLUSIVE_INPUT`/`ENABLE_WEBSOCKETS` option pattern.

## IR camera doesn't work while Steam is running

This is a known, well-documented conflict between Steam Input and Wiimotes,
not a bug in this module - see e.g. Valve's own bug tracker
("Cannot have steam input release control of the wii remote for dolphin
emulator") and multiple Steam Community reports of Steam Input opening and
actively driving Wiimotes even though they're not an officially supported
controller type, and not relinquishing control when asked.

**Mechanism:** WiiBrew documents that *any* status report - "requested or
unsolicited" - resets the Wiimote's data reporting mode at the firmware
level, regardless of which process's request triggered it. If Steam Input
is also polling the same Wiimote (which it does by default whenever it's
running, unless the device is excluded), its status requests silently reset
whichever report mode we most recently configured, including the
IR-carrying one - so IR data can stop arriving even though our own
`EnableIRCamera()` sequence succeeded and nothing is wrong with it.

**What this module does about it:** `WiimoteDevice::TickIRWatchdog()`
detects when IR data has gone stale despite `ir_enabled` being true, and
proactively re-asserts the report mode rather than waiting to notice via
our own next status request. This narrows the outage window but cannot
fully eliminate it if Steam is polling aggressively enough to win the race
repeatedly - the UI surfaces `WiimoteSnapshot::ir_possibly_hijacked` in that
case so the user isn't left wondering why IR silently stopped working, but
that flag can flicker rather than resolve, on a bad enough conflict.

**Actual fix (user-side, not code-side):** exclude the Wiimote from Steam
Input using Steam's `controller_blacklist` setting:

1. Fully quit Steam.
2. Find the Wiimote's VID/PID: `057e`/`0306` for the original Wii Remote,
   `057e`/`0330` for the Wii Remote Plus (see `WiimoteProtocol.h`'s
   `kVendorNintendo`/`kProductWiimote`/`kProductWiimotePlus`).
3. Edit `[Steam install]/config/config.vdf`, find (or add) the
   `controller_blacklist` entry, and add `"057e/0306"` (and/or
   `"057e/0330"`) to it.
4. Restart Steam.

Note some Steam client versions have shipped with this blacklist broken or
its UI removed entirely (multiple Steam Community bug reports referenced
above) - if it doesn't take effect, that's a Steam-side regression, not
something fixable from this module. The most reliable fallback in that case
is fully closing Steam while using the Wiimote's IR camera.

