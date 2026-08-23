#include "App/Log.h"
#include "DeviceManager.h"
#include "DeviceFactory.h"
#include "SensorReader.h"
#include "SDL3/SDL_joystick.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

static constexpr const char* kTag = "DeviceManager";

DeviceManager &DeviceManager::GetInstance() {
    static DeviceManager instance;
    return instance;
}

DeviceManager::DeviceManager() {
}

DeviceManager::~DeviceManager() { CloseAllDevices(); }

const std::vector<DeviceState> &DeviceManager::GetDevices() const { return m_Devices; }
std::vector<DeviceState>       &DeviceManager::GetDevices()       { return m_Devices; }

std::string DeviceManager::GetDeviceGUIDString(const DeviceState &dev) {
    SDL_Joystick* joystick = SDL_GetJoystickFromID(dev.instance_id);
    if (!joystick) {
        return "00000000000000000000000000000000";
    }
    SDL_GUID guid = SDL_GetJoystickGUID(joystick);
    char guidStr[33];
    SDL_GUIDToString(guid, guidStr, sizeof(guidStr));
    return std::string(guidStr);
}

void DeviceManager::HandleDeviceAdded(SDL_JoystickID instance_id) {
    // Wii Remote / Wii Remote Plus / Wii Balance Board are owned exclusively
    // by WiimoteManager over raw HID (see Devices/Wiimote/README.md) - they
    // expose IR/extension/Balance-Board data that has no representation in
    // SDL_Gamepad, so a generic DeviceState for them is actively wrong, not
    // just redundant.
    //
    // Disabling SDL_HINT_JOYSTICK_HIDAPI_WII in Application.cpp does NOT
    // reliably stop SDL from creating a joystick for these VID/PIDs - that
    // hint only opts out of SDL's *own* dedicated Wii HIDAPI driver, but
    // other SDL backends (raw HID gamepad heuristics, platform joystick
    // subsystems, etc, depending on OS/SDL version) can still pick the
    // device up and hand it to us as a plain "Gamepad" with none of its
    // real capabilities decoded. Worse, if we call SDL_OpenJoystick() on it
    // here (which CreateDevice() below does), that open can win the race
    // for the underlying HID handle before WiimoteManager::Scan() gets to
    // it, silently starving the real Wiimote support entirely - which is
    // exactly the "generic gamepad only, no real Wiimote panel" symptom
    // this filter fixes.
    //
    // VID/PID alone isn't reliable enough to catch this pre-open, in
    // practice: on at least one real-world Linux setup (CachyOS, Wiimote
    // over Bluetooth) SDL_GetJoystickVendorForID/ProductForID did NOT match
    // the expected 057e:0306/0330 for this device, even though the device
    // is genuinely a Wii Remote (its post-open SDL_GetJoystickName clearly
    // reads "Nintendo Wii Remote" - that's what ends up in the generic
    // panel's title when this filter fails to catch it). Root cause not
    // fully pinned down (likely a Linux joystick-backend quirk where
    // extended HID identifiers aren't populated until first open, possibly
    // specific to this BlueZ/hid-wiimote path) - rather than chase the
    // exact backend behavior, fall back to a name-based check too, since
    // the name is clearly available pre-open on every backend we've seen
    // (it's what's rendering in the screenshot that reported this bug).
    // Excludes "Wii U Pro Controller" deliberately - that's a distinct
    // device WiimoteManager doesn't implement (see README's gap list), and
    // works fine through SDL's normal gamepad path.
    const Uint16 vendor  = SDL_GetJoystickVendorForID(instance_id);
    const Uint16 product = SDL_GetJoystickProductForID(instance_id);
    const char  *name    = SDL_GetJoystickNameForID(instance_id);

    const bool vidpid_match = (vendor == InputBridge::Wiimote::kVendorNintendo) &&
        (product == InputBridge::Wiimote::kProductWiimote ||
         product == InputBridge::Wiimote::kProductWiimotePlus);
    const bool name_match = name && (
        std::strstr(name, "Wii Remote") != nullptr ||
        std::strstr(name, "RVL-CNT")    != nullptr ||
        std::strstr(name, "RVL-WBC")    != nullptr);
    const bool is_wii_u_pro = name && std::strstr(name, "Wii U Pro") != nullptr;

    if ((vidpid_match || name_match) && !is_wii_u_pro) {
        LOG_INFO(kTag, "Ignoring SDL joystick %d '%s' (vendor=0x%04x product=0x%04x, vidpid_match=%d name_match=%d) - handled by WiimoteManager",
                  instance_id, name ? name : "(null)", vendor, product, vidpid_match, name_match);
        // Don't wait for the next periodic ScanWiimotes() tick (up to
        // kWiimoteScanIntervalMs away) to pick this device up over raw
        // HID - scan right now, while SDL hasn't opened a joystick/gamepad
        // handle for it (this filter fired BEFORE CreateDevice() below),
        // so WiimoteManager gets the earliest possible shot at the node
        // before anything else has a chance to grab it.
        ScanWiimotes();
        m_LastWiimoteScanMs = SDL_GetTicks();
        return;
    }

    auto result = InputBridge::DeviceFactory::CreateDevice(instance_id);
    if (!result) {
        LOG_ERROR(kTag, "Failed to create device %d", instance_id);
        return;
    }

    // Defense-in-depth: the pre-open filter above depends on SDL_hid/SDL
    // capability queries that are, in practice, not reliably populated
    // pre-open on every backend (confirmed on at least one real Linux/
    // Bluetooth setup, where vendor/product came back wrong for a genuine
    // Wiimote even though its post-open name was correct). Now that
    // CreateDevice() has actually opened it, result->state.name comes from
    // SDL_GetJoystickName() - the same call DeviceFactory already uses and
    // that reliably returns "Nintendo Wii Remote" etc, per the report that
    // led to this check. Catch it here too and back the open out, rather
    // than leaving a wrong DeviceState in place until the next restart.
    {
        const std::string &n = result->state.name;
        const bool post_open_match =
            (n.find("Wii Remote") != std::string::npos ||
             n.find("RVL-CNT")    != std::string::npos ||
             n.find("RVL-WBC")    != std::string::npos) &&
            n.find("Wii U Pro") == std::string::npos;
        if (post_open_match) {
            LOG_INFO(kTag, "Joystick %d ('%s') slipped past the pre-open Wiimote filter - "
                             "closing it now and leaving it for WiimoteManager", instance_id, n.c_str());
            // Mirrors CloseAllDevices()'s close order below: a gamepad
            // handle owns its underlying joystick, so closing the joystick
            // directly when it was opened via SDL_OpenGamepad (state.gamepad
            // set) would be wrong - close whichever one was actually opened.
            if (result->state.gamepad)
                SDL_CloseGamepad(result->state.gamepad);
            else if (result->state.joystick)
                SDL_CloseJoystick(result->state.joystick);
            // Retry the raw-HID scan now that SDL's own handle on this node
            // has just been released - WiimoteManager's SDL_hid_open_path()
            // was very likely failing against that still-open handle until
            // this exact point, so waiting for the next periodic tick would
            // just repeat the same failure for up to kWiimoteScanIntervalMs
            // longer than necessary.
            ScanWiimotes();
            m_LastWiimoteScanMs = SDL_GetTicks();
            return;
        }
    }

    m_Devices.push_back(std::move(result->state));
    
    // Initialize battery info for the new device
    UpdateBatteryInfo(m_Devices.back());
    
    // -- Enable all IMU sensors at connect-time ------------------------------
    // SDL3 requires sensors to be enabled before the first read. For a combined
    // Joy-Con pair, SDL_SENSOR_GYRO_L and SDL_SENSOR_ACCEL_L (left Joy-Con)
    // are separate streams from SDL_SENSOR_GYRO_R / SDL_SENSOR_ACCEL_R (right).
    // Without an explicit enable here, ReadGyroL / ReadAccelL always return
    // available=false because SDL never starts the left-side sensor pipeline.
    if (m_Devices.back().gamepad)
        SensorReader::EnableAll(m_Devices.back().gamepad);
    // -- End sensor enable --------------------------------------------------- 

    if (result->haptic) {
        m_HapticDevices[instance_id] = std::move(result->haptic);
    }
}

void DeviceManager::HandleDeviceRemoved(SDL_JoystickID instance_id) {
    // CRITICAL FIX: Manually close the haptic device before erasing
    // This ensures SDL_CloseHaptic is called before SDL_CloseGamepad/Joystick
    // which prevents double-free when the gamepad close also closes the haptic
    auto haptic_it = m_HapticDevices.find(instance_id);
    if (haptic_it != m_HapticDevices.end()) {
        // Manually call Close() to clean up haptic before SDL closes it
        if (haptic_it->second) {
            haptic_it->second->Close();
        }
        // Now erase (destructor will be called but Close() is idempotent)
        m_HapticDevices.erase(haptic_it);
    }
    
    auto it = std::remove_if(m_Devices.begin(), m_Devices.end(), [instance_id](const DeviceState &dev) {
        if (dev.instance_id == instance_id) {
            if (dev.gamepad)
                SDL_CloseGamepad(dev.gamepad);
            else if (dev.joystick)
                SDL_CloseJoystick(dev.joystick);
            return true;
        }
        return false;
    });

    if (it != m_Devices.end()) {
        m_Devices.erase(it, m_Devices.end());
    }

    // If no steering wheels remain, clear the device list.
    bool anyWheelLeft = false;
    for (const auto& dev : m_Devices) {
        if (SDL_GetJoystickTypeForID(dev.instance_id) == SDL_JOYSTICK_TYPE_WHEEL) {
            anyWheelLeft = true;
            break;
        }
    }
}

void DeviceManager::CloseAllDevices() {
    // Close haptic devices first (before their joysticks are closed)
    for (auto& pair : m_HapticDevices) {
        if (pair.second) {
            pair.second->Close();
        }
    }
    m_HapticDevices.clear();

    // Release Wiimotes (each destructor closes its HID handle)
    m_Wiimotes.clear();

    // Now close SDL devices
    for (auto &dev : m_Devices) {
        if (dev.gamepad)
            SDL_CloseGamepad(dev.gamepad);
        else if (dev.joystick)
            SDL_CloseJoystick(dev.joystick);
    }
    m_Devices.clear();
}

// ---------------------------------------------------------------------------
// Wiimote / Balance Board / Nunchuk / Classic Controller / Guitar Hero
// ---------------------------------------------------------------------------

void DeviceManager::ScanWiimotes() {
    // Prune stale entries FIRST, so a since-vanished path frees up before we
    // build the "don't touch these" list below - otherwise a genuinely
    // reconnected device (same path, new physical pairing) could be kept
    // out of Scan() by its own stale ghost entry until the next cycle.
    constexpr Uint64 kStaleTimeoutMs = 5000;
    const Uint64 now = SDL_GetTicks();
    auto stale_it = std::remove_if(m_Wiimotes.begin(), m_Wiimotes.end(), [&](const auto &dev) {
        const auto &snap = dev->Snapshot();
        return snap.last_report_ms != 0 && (now - snap.last_report_ms) > kStaleTimeoutMs;
    });
    if (stale_it != m_Wiimotes.end()) {
        LOG_INFO(kTag, "Wiimote(s) went stale, removing %td", std::distance(stale_it, m_Wiimotes.end()));
        m_Wiimotes.erase(stale_it, m_Wiimotes.end());
    }

    // Tell Scan() which HID paths we already have open, so it never opens a
    // second concurrent handle to a device we're already tracking - see the
    // hazard documented on WiimoteManager::Scan() (races Init() against
    // itself on the same physical device, which is the kind of thing that
    // can crash rather than just misbehave on some Bluetooth HID stacks).
    std::vector<std::string> already_open;
    already_open.reserve(m_Wiimotes.size());
    for (auto &dev : m_Wiimotes) already_open.push_back(dev->Snapshot().hid_path);

    auto found = InputBridge::Wiimote::WiimoteManager::Scan(already_open);
    for (auto &dev : found) {
        LOG_INFO(kTag, "Wiimote found: %s", dev->Snapshot().hid_path.c_str());
        m_Wiimotes.push_back(std::move(dev));
    }
}

const std::vector<std::unique_ptr<InputBridge::Wiimote::WiimoteDevice>>&
DeviceManager::GetWiimotes() const {
    return m_Wiimotes;
}

void DeviceManager::Update(bool isMinimized) {
    // Refresh battery info every 5 seconds when active,
    // or every 30 seconds when minimized.
    // Controller battery levels change very slowly, so high-frequency
    // polling is unnecessary and CPU-intensive on some platforms.
    static Uint64 lastBatteryUpdate = 0;
    Uint64 now = SDL_GetTicks();

    // If minimized, we multiply the interval by 6 (e.g. 5s -> 30s)
    // to further reduce background overhead.
    Uint64 interval = isMinimized ? (m_BatteryUpdateIntervalMs * 6) : m_BatteryUpdateIntervalMs;

    if (now - lastBatteryUpdate > interval) {
        for (auto &dev : m_Devices) {
            UpdateBatteryInfo(dev);
        }
        lastBatteryUpdate = now;
    }

    // -- Wiimote polling --------------------------------------------------
    // Unlike SDL_Joystick devices, Wiimotes pair over Bluetooth outside
    // SDL's own joystick hotplug events, so we periodically re-scan for new
    // ones rather than relying solely on HandleDeviceAdded(). Every tracked
    // device gets drained every frame (each Poll() is a cheap no-op when
    // nothing is pending, since the underlying handle is non-blocking).
    if (m_LastWiimoteScanMs == 0 || now - m_LastWiimoteScanMs > kWiimoteScanIntervalMs) {
        ScanWiimotes();
        m_LastWiimoteScanMs = now;
    }
    for (auto &dev : m_Wiimotes) {
        dev->Poll();
    }
    // -- End Wiimote polling ----------------------------------------------
}

HapticDevice *DeviceManager::GetHapticDevice(SDL_JoystickID instance_id) const {
    auto it = m_HapticDevices.find(instance_id);
    if (it != m_HapticDevices.end()) {
        return it->second.get();
    }
    return nullptr;
}

void DeviceManager::SetDeviceKeepalive(SDL_JoystickID instance_id, bool enable) {
    HapticDevice* haptic = GetHapticDevice(instance_id);
    if (haptic) haptic->EnableKeepalive(enable);
}

void DeviceManager::UpdateBatteryInfo(DeviceState &dev) {
    SDL_PowerState old_state = dev.battery_state;
    int old_percent = dev.battery_percent;

    if (dev.gamepad) {
        int percent = 0;
        dev.battery_state = SDL_GetGamepadPowerInfo(dev.gamepad, &percent);
        dev.battery_percent = percent;

        bool state_changed   = (old_state != dev.battery_state);
        // Only compare percents when both readings are valid numbers.
        bool percent_changed = (dev.battery_percent >= 0 && old_percent >= 0 &&
                                abs(old_percent - dev.battery_percent) >= 5);

        if (!dev.battery_initialized || state_changed || percent_changed) {
            dev.battery_initialized = true;

            const char* state_str;
            switch (dev.battery_state) {
                case SDL_POWERSTATE_UNKNOWN:    state_str = "UNKNOWN";    break;
                case SDL_POWERSTATE_ON_BATTERY: state_str = "ON_BATTERY"; break;
                case SDL_POWERSTATE_NO_BATTERY: state_str = "NO_BATTERY"; break;
                case SDL_POWERSTATE_CHARGING:   state_str = "CHARGING";   break;
                case SDL_POWERSTATE_CHARGED:    state_str = "CHARGED";    break;
                default:                        state_str = "INVALID";    break;
            }

            if (dev.battery_state == SDL_POWERSTATE_UNKNOWN) {
                LOG_WARN(kTag, "Battery [%s]: State=%s (battery info not available)",
                        dev.name.c_str(), state_str);
                LOG_WARN(kTag, "Possible causes: hid_playstation not loaded, missing udev rules, or SDL can't read battery");
            } else if (dev.battery_state != SDL_POWERSTATE_NO_BATTERY) {
                LOG_DEBUG(kTag, "Battery [%s]: State=%s, Percent=%d%%",
                        dev.name.c_str(), state_str, percent);
            }
        }

        // -- Left Joy-Con battery (combined pair only) ---------------------
        // When two Joy-Cons are merged into one virtual gamepad, SDL exposes
        // SDL_SENSOR_GYRO_L on the combined handle.  In that case we find the
        // left Joy-Con's physical joystick by scanning all connected joystick
        // IDs for the sibling that SDL merged into this gamepad.
        // SDL_GetGamepadID() returns the instance_id of the *right* Joy-Con
        // (the primary half); the left half is the other HIDAPI joystick whose
        // type is GAMEPAD and whose instance_id differs from dev.instance_id.
        if (SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_L)) {
            dev.battery_state_L   = SDL_POWERSTATE_UNKNOWN;
            dev.battery_percent_L = -1;

            int joystick_count = 0;
            SDL_JoystickID* joystick_ids = SDL_GetJoysticks(&joystick_count);
            if (joystick_ids) {
                for (int i = 0; i < joystick_count; ++i) {
                    SDL_JoystickID jid = joystick_ids[i];
                    if (jid == dev.instance_id) continue;
                    if (SDL_GetJoystickTypeForID(jid) != SDL_JOYSTICK_TYPE_GAMEPAD) continue;

                    // Open transiently just to read battery; SDL ref-counts opens.
                    SDL_Joystick* joy = SDL_OpenJoystick(jid);
                    if (!joy) continue;

                    int leftPercent = 0;
                    SDL_PowerState leftState = SDL_GetJoystickPowerInfo(joy, &leftPercent);

                    if (leftState != SDL_POWERSTATE_NO_BATTERY) {
                        dev.battery_state_L   = leftState;
                        dev.battery_percent_L = leftPercent;
                        LOG_DEBUG(kTag, "Battery L [%s]: Percent=%d%%", dev.name.c_str(), leftPercent);
                        SDL_CloseJoystick(joy);
                        break;
                    }

                    SDL_CloseJoystick(joy);
                }
                SDL_free(joystick_ids);
            }
        } else {
            // Not a combined pair - clear the left-side fields.
            dev.battery_state_L   = SDL_POWERSTATE_UNKNOWN;
            dev.battery_percent_L = -1;
        }
        // -- End Left Joy-Con battery --------------------------------------
    } else if (dev.joystick) {
        int percent = 0;
        dev.battery_state = SDL_GetJoystickPowerInfo(dev.joystick, &percent);
        dev.battery_percent = percent;

        bool state_changed   = (old_state != dev.battery_state);
        bool percent_changed = (dev.battery_percent >= 0 && old_percent >= 0 &&
                                abs(old_percent - dev.battery_percent) >= 5);

        if (!dev.battery_initialized || state_changed || percent_changed) {
            dev.battery_initialized = true;

            const char* state_str;
            switch (dev.battery_state) {
                case SDL_POWERSTATE_UNKNOWN:    state_str = "UNKNOWN";    break;
                case SDL_POWERSTATE_ON_BATTERY: state_str = "ON_BATTERY"; break;
                case SDL_POWERSTATE_NO_BATTERY: state_str = "NO_BATTERY"; break;
                case SDL_POWERSTATE_CHARGING:   state_str = "CHARGING";   break;
                case SDL_POWERSTATE_CHARGED:    state_str = "CHARGED";    break;
                default:                        state_str = "INVALID";    break;
            }

            if (dev.battery_state != SDL_POWERSTATE_NO_BATTERY && dev.battery_state != SDL_POWERSTATE_UNKNOWN) {
                LOG_DEBUG(kTag, "Battery (Joystick) [%s]: State=%s, Percent=%d%%",
                        dev.name.c_str(), state_str, percent);
            }
        }
    } else {
        dev.battery_state   = SDL_POWERSTATE_UNKNOWN;
        dev.battery_percent = -1;
    }
}
// ---------------------------------------------------------------------------
// Device hide support
// ---------------------------------------------------------------------------

bool DeviceManager::IsHideAvailable() const {
#ifdef ENABLE_EXCLUSIVE_INPUT
    return m_HideManager.IsAvailable();
#else
    return false;
#endif
}

bool DeviceManager::SetDeviceHidden(DeviceState& dev, bool hidden) {
#ifdef ENABLE_EXCLUSIVE_INPUT
    SDL_Joystick* joy = dev.joystick
                         ? dev.joystick
                         : (dev.gamepad ? SDL_GetGamepadJoystick(dev.gamepad) : nullptr);
    if (!joy) {
        LOG_WARN(kTag, "SetDeviceHidden: no joystick handle for '%s'.",
                dev.name.c_str());
        return false;
    }

    bool ok = m_HideManager.SetHidden(joy, hidden);
    if (ok) dev.hide_from_other_apps = hidden;
    return ok;
#else
    (void)dev; (void)hidden;
    LOG_WARN(kTag, "SetDeviceHidden: exclusive input support not compiled in.");
    return false;
#endif
}

void DeviceManager::SetSteamInputCompatible(bool enabled) {
#ifdef ENABLE_EXCLUSIVE_INPUT
    m_HideManager.SetSteamInputCompatible(enabled);
#else
    (void)enabled;
#endif
}
