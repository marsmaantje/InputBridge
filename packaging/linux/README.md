# Linux packaging: Wiimote/Balance Board udev rules

## Why this exists

InputBridge talks to the Wii Remote and Wii Balance Board over Linux's
`hidraw` interface (`/dev/hidraw*`). When one of these is connected via a
USB Bluetooth **dongle**, the resulting `hidraw` node is frequently created
`root:root` with mode `0600` by the kernel/udev, because no rule tags it
for the desktop user otherwise - some distros only auto-grant access to
device classes they specifically whitelist, and a dongle's plain
`hid-generic`/`uhid`-backed HID path doesn't always land in that list the
way a laptop's built-in Bluetooth adapter's devices might. Without a rule,
InputBridge fails to open the device and logs an `EACCES` "permission
denied" diagnostic (see `WiimoteManager.cpp`).

## What's here

- `udev/71-inputbridge-wiimote.rules` - a udev rule matching Nintendo's
  vendor ID (`057e`) and the Wiimote/Wiimote Plus/Balance Board product
  IDs (`0306`, `0330`), tagged both with `uaccess` (for modern
  systemd-logind-based distros) and `GROUP="plugdev", MODE="0660"` (for
  distros that still rely on group ownership instead).
- `install-udev-rules.sh` - installs (or `--uninstall`s) that rule to
  `/etc/udev/rules.d/`, reloads udev, and adds the invoking user to
  `plugdev` if that group exists and they're not already in it.

Both are installed by the CMake Linux packaging step to
`share/inputbridge/udev/` alongside the app - they are **not** applied
automatically by `cmake --install`, since writing to `/etc/udev/rules.d`
is a host system change that shouldn't happen implicitly or require a
plain build/install step to run as root.

## Usage

After installing InputBridge:

```sh
sudo <install-prefix>/share/inputbridge/udev/install-udev-rules.sh
```

or, running straight from the source tree:

```sh
sudo ./packaging/linux/install-udev-rules.sh
```

Then unplug/replug the Bluetooth dongle (or the Wiimote/Balance Board, if
paired directly) so the new rule applies, and re-launch InputBridge.

To remove the rule later:

```sh
sudo ./packaging/linux/install-udev-rules.sh --uninstall
```

## Flatpak

The Flatpak build (`org.inputbridge.InputBridge.yml`) grants
`--filesystem=/etc/udev/rules.d:create` so the sandboxed app sees the
*real* host `/etc/udev/rules.d` instead of the runtime's own private
copy - without it, "Check for common issues" would report the rule as
never installed regardless of what's actually on the host, and a rule
installed from inside the sandbox wouldn't end up anywhere udevd on the
host would ever read.

It also grants `--talk-name=org.freedesktop.Flatpak` so the in-app
Install/Remove buttons can run `flatpak-spawn --host pkexec ...` - a
plain `pkexec` call from inside the sandbox can't reach the host's
PolicyKit authority at all. `LinuxUdevInstaller` stages a copy of this
script and the `.rules` file under `$XDG_RUNTIME_DIR` first, since
`flatpak-spawn --host`'s argv is resolved on the host and the app's own
`/app/share/...` copy isn't visible there.

If you're building or auditing the Flatpak manifest yourself and either
permission is missing, expect exactly those two symptoms: a permanently
"not installed" diagnostic despite a real rule on the host, and/or a
disabled or failing Install/Remove button.

## Packagers (deb/rpm/AUR/etc.)

If you're building a distro package, prefer installing
`udev/71-inputbridge-wiimote.rules` directly to `/etc/udev/rules.d/` (or
your packaging format's udev rules hook) as part of the package's normal
post-install, rather than shipping and asking users to run the script -
the script exists for source builds and generic tarball/AppImage-style
installs where there's no packaging-format post-install hook to use.