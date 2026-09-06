#!/usr/bin/env bash
# InputBridge - install udev rules for Wii Remote / Wii Balance Board
#
# Grants InputBridge (running as a normal user) permission to open the
# /dev/hidraw* node a Wiimote or Balance Board shows up as. This is most
# often needed when the device is connected through a USB Bluetooth
# dongle rather than a laptop's built-in adapter - the resulting hidraw
# node is created root:root/0600 by default on many distros unless a
# rule tags it otherwise.
#
# Usage:
#   ./install-udev-rules.sh            # install
#   ./install-udev-rules.sh --uninstall
#   sudo ./install-udev-rules.sh       # also fine; re-execs via sudo either way
#
# Safe to re-run. Only touches the one rules file this script owns.

set -euo pipefail

RULES_FILENAME="99-inputbridge-wiimote.rules"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
DEST_RULES_PATH="/etc/udev/rules.d/${RULES_FILENAME}"

# Installed layout (CMake `install()`) puts the rules file next to this
# script (share/inputbridge/udev/); the source tree keeps it one level
# down (packaging/linux/udev/). Try both so the script works either way.
if [[ -f "${SCRIPT_DIR}/${RULES_FILENAME}" ]]; then
    SOURCE_RULES_PATH="${SCRIPT_DIR}/${RULES_FILENAME}"
else
    SOURCE_RULES_PATH="${SCRIPT_DIR}/udev/${RULES_FILENAME}"
fi

UNINSTALL=0
for arg in "$@"; do
    case "$arg" in
        --uninstall) UNINSTALL=1 ;;
        -h|--help)
            sed -n '2,18p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            exit 1
            ;;
    esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "error: udev rules only apply on Linux; nothing to do on $(uname -s)." >&2
    exit 1
fi

# Re-exec under sudo if not already root, so the script can be double
# clicked / run directly by a user without them having to remember the
# sudo prefix themselves.
if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "error: this needs root to write to /etc/udev/rules.d, and 'sudo' isn't available." >&2
        echo "       re-run this script as root instead." >&2
        exit 1
    fi
    exec sudo -- "$0" "$@"
fi

reload_udev() {
    if ! command -v udevadm >/dev/null 2>&1; then
        echo "warning: udevadm not found - reload the rule manually or replug the device." >&2
        return
    fi
    udevadm control --reload-rules
    udevadm trigger --subsystem-match=hidraw
}

if [[ "${UNINSTALL}" -eq 1 ]]; then
    if [[ -e "${DEST_RULES_PATH}" ]]; then
        rm -f -- "${DEST_RULES_PATH}"
        echo "Removed ${DEST_RULES_PATH}"
    else
        echo "Nothing to remove (${DEST_RULES_PATH} not present)."
    fi
    reload_udev
    echo "Done. Unplug and replug the Wiimote/Balance Board (or its Bluetooth dongle) to apply."
    exit 0
fi

if [[ ! -f "${SOURCE_RULES_PATH}" ]]; then
    echo "error: expected to find ${SOURCE_RULES_PATH} next to this script, but it's missing." >&2
    exit 1
fi

install -D -m 0644 -- "${SOURCE_RULES_PATH}" "${DEST_RULES_PATH}"
echo "Installed ${DEST_RULES_PATH}"

reload_udev

# Best-effort: if this distro uses plugdev and the invoking (pre-sudo) user
# isn't in it yet, add them, since the rule falls back to plugdev on
# systems without uaccess. Harmless no-op on uaccess-only distros.
INVOKING_USER="${SUDO_USER:-}"
if [[ -n "${INVOKING_USER}" ]] && getent group plugdev >/dev/null 2>&1; then
    if ! id -nG "${INVOKING_USER}" 2>/dev/null | tr ' ' '\n' | grep -qx plugdev; then
        usermod -aG plugdev "${INVOKING_USER}"
        echo "Added user '${INVOKING_USER}' to the 'plugdev' group (log out/in to take effect)."
    fi
fi

cat <<'EOF'

Done. Next steps:
  1. Unplug and replug the Bluetooth dongle (or the Wiimote/Balance Board
     itself, if paired directly) so the new rule applies to it.
  2. If you were just added to the 'plugdev' group, log out and back in
     (group membership changes don't apply to already-open sessions).
  3. Launch InputBridge and try connecting the device again.

To remove this rule later: ./install-udev-rules.sh --uninstall
EOF