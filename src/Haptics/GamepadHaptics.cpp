#include "App/Log.h"
#include "GamepadHaptics.h"
#include <SDL3/SDL_gamepad.h>
#include "Core/Result.h"
#include <algorithm>

static constexpr const char* kTag = "GamepadHaptics";

// ==================== Destructor ====================

GamepadHaptics::~GamepadHaptics() {
}

void GamepadHaptics::StopAll() {
    // Send the "off" adaptive trigger command first (synchronous HID write).
    // The base class only clears the m_activeDualSenseTriggers tracking map
    // used for UI display - it never tells the controller hardware to
    // release trigger tension, so a DualSense would otherwise stay
    // physically resisted after effects are "stopped" (e.g. on app
    // shutdown, via StopAllHapticEffects()). No-op on non-DualSense
    // controllers.
    if (m_dualSense) {
        PlayDualSenseTrigger("both", "off", {});
    }

    // Zero both impulse trigger motors for the same reason - the base class
    // only clears m_activeXboxTrigger for UI display, it never tells the
    // controller hardware to stop the motors. No-op on non-Xbox controllers.
    if (m_xbox) {
        PlayXboxTrigger(0, 0, 0);
    }

    // Clears rumble/constant/periodic/condition effects and their SDL
    // effect IDs, plus the m_activeDualSenseTriggers/m_activeXboxTrigger
    // tracking state. This runs asynchronously on the device's worker
    // thread, so it's queued after (not before) the synchronous off-commands
    // above - otherwise the entries just recorded could be cleared out of
    // order and leave stale entries in the tracking state.
    HapticDevice::StopAll();
}

// ==================== Initialization ====================

InputBridge::Result<bool, InputBridge::HapticError> GamepadHaptics::Init() {
    // Initialize base haptic device
    auto result = HapticDevice::Init();

    // Resolve and cache the SDL_Gamepad* handle once.  SDL_GetGamepadFromID
    // may return null later for some controller/driver combinations (e.g. the
    // V2 Steam Controller via SDL_hidapi_steam_triton.c), so caching here at
    // open time - when DeviceFactory has just called SDL_OpenGamepad - is the
    // only reliable way to obtain it.
    SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
    m_gamepad = SDL_GetGamepadFromID(id);

    // Create specialized controllers based on hardware
    if (IsDualSense()) {
        m_dualSense = std::make_unique<DualSenseController>(m_joystick);
        m_dualSense->Init();
        LOG_INFO(kTag, "Initialized as DualSense");
    }
    else if (IsXboxController()) {
        m_xbox = std::make_unique<XboxController>(m_joystick);
        m_xbox->Init();
        LOG_INFO(kTag, "Initialized as Xbox");
    }
    else if (IsSteamControllerV1()) {
        // V1 haptic pulses are sent via SDL_SendGamepadEffect in SendSteamControllerHaptic(),
        // which routes through SDL's already-open HIDAPI handle.  No separate HID open needed.
        LOG_INFO(kTag, "Initialized as Steam Controller V1 (trackpad HID haptics)");
    }
    else if (IsSteamControllerV2()) {
        // V2 supports SDL_RumbleGamepad natively since SDL 3.4.10.
        // No special initialization required - the standard rumble path is used.
        LOG_INFO(kTag, "Initialized as Steam Controller V2 (SDL_RumbleGamepad)");
    }
    else {
        LOG_INFO(kTag, "Initialized as generic gamepad");
    }

    return result;
}

bool GamepadHaptics::IsReady() const {
    // Check base class (SDL haptic handle open)
    if (HapticDevice::IsReady()) {
        return true;
    }

    // Check if we have a gamepad handle (covers SDL rumble path)
    if (m_gamepad) {
        return true;
    }

    // Check specialized controllers
    if (m_dualSense && m_dualSense->IsReady()) {
        return true;
    }
    if (m_xbox && m_xbox->IsReady()) {
        return true;
    }

    return false;
}

// ==================== Controller Detection ====================

bool GamepadHaptics::IsDualSense() const {
    const Uint16 vendor  = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);

    return (vendor == 0x054C &&       // Sony
            (product == 0x0CE6 ||     // DualSense
             product == 0x0DF2));     // DualSense Edge
}

bool GamepadHaptics::IsXboxController() const {
    // Detection lives solely in XboxController - delegate to its static
    // check rather than keeping a second, separately-maintained copy of the
    // vendor/product ID list here. (That duplication previously drifted:
    // this copy was missing the wired/BLE Series X|S product IDs that
    // XboxController's own list had already been updated with.)
    return XboxController::IsXboxController(m_joystick);
}

bool GamepadHaptics::IsSteamController() const {
    return IsSteamControllerV1() || IsSteamControllerV2();
}

bool GamepadHaptics::IsSteamControllerV1() const {
    const Uint16 vid = SDL_GetJoystickVendor(m_joystick);
    const Uint16 pid = SDL_GetJoystickProduct(m_joystick);

    if (vid != VALVE_VENDOR_ID)
        return false;

    // Exclude all V2 PIDs (HEADCRAB + TRITON) so this function is safe to
    // call in any order.
    if (IsSteamControllerV2())
        return false;

    switch (pid)
    {
        case STEAM_CONTROLLER_USB_PID: // Wired D0G
        case 0x1105: // Bluetooth D0G
        case STEAM_CONTROLLER_WIRELESS_PID: // Bluetooth D0G
        case 0x1142: // Wireless dongle
            return true;

        default:
            return false;
    }
}

bool GamepadHaptics::IsSteamControllerV2() const {
    const Uint16 vendor  = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);

    if (vendor != VALVE_VENDOR_ID)
        return false;

    // HEADCRAB hardware (SDL_hidapi_steam.c, present since SDL 3.x)
    if (product == STEAM_CONTROLLER_V2_USB_PID ||
        product == STEAM_CONTROLLER_V2_BT_PID)
        return true;

    // TRITON hardware (SDL_hidapi_steam_triton.c, added SDL 3.4.10).
    // SDL reports all four Triton PIDs under the generic "Steam Controller"
    // name (HIDAPI_SetDeviceName in InitDevice), so we must detect them by
    // PID to produce the correct "Steam Controller V2" tab label.
    if (product == STEAM_CONTROLLER_TRITON_USB_PID ||
        product == STEAM_CONTROLLER_TRITON_BLE_PID ||
        product == STEAM_CONTROLLER_PROTEUS_PID     ||
        product == STEAM_CONTROLLER_NEREID_PID)
        return true;

    return false;
}

const char* GamepadHaptics::GetControllerTypeName() const {
    if (IsDualSense())          return "DualSense";
    if (IsXboxController())     return "Xbox";
    // V2 must be checked before V1: IsSteamControllerV1() includes the
    // undocumented 0x1105 PID which can overlap with certain V2 firmware
    // revisions.  Checking V2 first guarantees the correct name is returned
    // regardless of PID ordering, consistent with the PlayRumble guard
    // (!IsSteamControllerV2() && IsSteamControllerV1()).
    if (IsSteamControllerV2())  return "Steam Controller V2";
    if (IsSteamControllerV1())  return "Steam Controller V1";
    return "Generic Gamepad";
}

// ==================== Universal Rumble ====================

int GamepadHaptics::PlayRumble(int slot, float largeMagnitude, float smallMagnitude, uint32_t durationMs) {
    largeMagnitude = std::clamp(largeMagnitude, 0.0f, 1.0f);
    smallMagnitude = std::clamp(smallMagnitude, 0.0f, 1.0f);

    RunAsync([this, slot, largeMagnitude, smallMagnitude, durationMs]() {

        // ── Steam Controller V1 path (D0G, PIDs 0x1102 / 0x1106) ─────────────
        // V1 has no rumble motors; SDL_RumbleGamepad does nothing on it.
        // Haptic feedback requires vendor HID reports to the trackpad actuators.
        //
        // IsSteamControllerV2() is checked first to ensure V2 hardware (which
        // shares the "Steam Controller" name string) is never caught by V1's
        // name-based fallback and routed here by mistake.
        if (!IsSteamControllerV2() && IsSteamControllerV1()) {
            const float magnitude = (largeMagnitude + smallMagnitude) * 0.5f;
            LOG_DEBUG(kTag, "PlayRumble - Steam Controller V1: HID trackpad haptic (magnitude=%.2f, duration=%ums)",
                    magnitude, durationMs);

            SendSteamControllerHaptic(0, magnitude, durationMs); // left  trackpad
            SendSteamControllerHaptic(1, magnitude, durationMs); // right trackpad

            if (magnitude > 0.0f) {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info           = m_activeRumbles[slot];
                info.active          = true;
                info.large_magnitude = largeMagnitude;
                info.small_magnitude = smallMagnitude;
                info.duration_ms     = durationMs;
                info.last_updated    = SDL_GetTicks();
            }
            return;
        }

        // ── Standard SDL path (all other gamepads, including Steam Controller V2) ─
        if (m_gamepad) {
            const Uint16 lowFreq  = static_cast<Uint16>(largeMagnitude * 0xFFFF);
            const Uint16 highFreq = static_cast<Uint16>(smallMagnitude * 0xFFFF);

            if (!SDL_RumbleGamepad(m_gamepad, lowFreq, highFreq, durationMs)) {
                LOG_WARN(kTag, "PlayRumble - SDL_RumbleGamepad failed: %s", SDL_GetError());
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                m_activeRumbles.erase(slot);
            } else {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info           = m_activeRumbles[slot];
                info.active          = true;
                info.large_magnitude = largeMagnitude;
                info.small_magnitude = smallMagnitude;
                info.duration_ms     = durationMs;
                info.last_updated    = SDL_GetTicks();
                LOG_DEBUG(kTag, "PlayRumble - Success slot=%d (low=%u, high=%u, duration=%ums)",
                        slot, lowFreq, highFreq, durationMs);
            }
        } else {
            LOG_WARN(kTag, "PlayRumble - No gamepad handle available");
        }
    });

    return 0;
}

// ==================== Steam Controller HID Haptics ====================

/**
 * Sends a haptic pulse to one Steam Controller V1 trackpad via SDL_SendGamepadEffect
 * or SDL_SendJoystickEffect (whichever handle is available).
 *
 * SDL's HIDAPI Steam driver (SDL_hidapi_steam.c) implements SendJoystickEffect for
 * exactly 65-byte payloads - it calls SDL_hid_send_feature_report on its
 * already-open device handle.  We build a FeatureReportMsg (from Valve's
 * controller_structs.h) with command ID_TRIGGER_HAPTIC_PULSE (0x8F):
 *
 *   Byte  0   : reserved / report ID (0x00 - SDL prepends this)
 *   Byte  1   : command = ID_TRIGGER_HAPTIC_PULSE (0x8F)
 *   Byte  2   : payload length = sizeof(MsgFireHapticPulse) = 9
 *   Byte  3   : which_pad (0 = left trackpad, 1 = right trackpad)
 *   Bytes 4–5 : pulse_duration in microseconds (little-endian Uint16)
 *   Bytes 6–7 : pulse_interval in microseconds (little-endian Uint16)
 *   Bytes 8–9 : pulse_count (little-endian Uint16)
 *   Bytes 10–11: dBgain (Sint16, 0 = default)
 *   Byte 12   : priority flags (0 = HAPTIC_PULSE_NORMAL)
 *   Bytes 13–64: padding / unused payload bytes (zeroed)
 *
 * A fixed period of 5 000 µs (5 ms) is used.  Magnitude scales the on-time
 * within each period (duty cycle): full magnitude → pulse_duration == period
 * (maximum continuous vibration); lower values shorten the on-time.
 * pulse_count is derived from the requested duration so the effect
 * self-terminates on the hardware without needing repeated writes.
 *
 * This function is V1-only (PIDs 0x1102 / 0x1106).  V2 (0x1201 / 0x1202)
 * uses SDL_RumbleGamepad via SDL_hidapi_steam_triton.c and never calls here.
 *
 * Reference: Valve's open-source controller_structs.h / controller_constants.h
 *            and SDL's SDL_hidapi_steam.c (HIDAPI_DriverSteam_SendJoystickEffect).
 */
void GamepadHaptics::SendSteamControllerHaptic(uint8_t pad, float magnitude, uint32_t durationMs) {
    magnitude = std::clamp(magnitude, 0.0f, 1.0f);
    if (magnitude <= 0.0f) return;

    // Period is fixed at 5 000 µs.  Duration scales with magnitude (duty cycle).
    const uint16_t periodUs   = 5000;
    const uint16_t durationUs = static_cast<uint16_t>(magnitude * static_cast<float>(periodUs));

    // pulse_count: how many period-length pulses fill the requested wall-clock duration.
    const uint16_t pulseCount = static_cast<uint16_t>(
        std::max(1u, (durationMs + STEAM_HAPTIC_PULSE_DURATION_MS - 1)
                     / STEAM_HAPTIC_PULSE_DURATION_MS));

    // Build the 65-byte FeatureReportMsg.
    // Byte 0 is the HID report ID (0x00 for feature reports sent via SDL).
    // Byte 1 is the Valve command byte.
    // Byte 2 is the payload length (sizeof MsgFireHapticPulse = 9).
    // Bytes 3–11 are the MsgFireHapticPulse fields.
    // Remaining bytes are zero-padded.
    uint8_t report[STEAM_FEATURE_REPORT_SIZE] = {};
    report[1] = STEAM_HAPTIC_PULSE_MSG_ID;   // 0x8F - ID_TRIGGER_HAPTIC_PULSE
    report[2] = 9;                           // payload length = sizeof(MsgFireHapticPulse)
    report[3] = pad;                         // which_pad: 0 = left, 1 = right
    report[4] = static_cast<uint8_t>(durationUs & 0xFF);          // pulse_duration lo
    report[5] = static_cast<uint8_t>((durationUs >> 8) & 0xFF);   // pulse_duration hi
    report[6] = static_cast<uint8_t>(periodUs & 0xFF);            // pulse_interval lo
    report[7] = static_cast<uint8_t>((periodUs >> 8) & 0xFF);     // pulse_interval hi
    report[8] = static_cast<uint8_t>(pulseCount & 0xFF);          // pulse_count lo
    report[9] = static_cast<uint8_t>((pulseCount >> 8) & 0xFF);   // pulse_count hi
    // report[10–11] = dBgain (Sint16) - 0 = default gain
    // report[12]    = priority flags - 0 = HAPTIC_PULSE_NORMAL

    LOG_DEBUG(kTag, "SendSteamControllerHaptic - pad=%u magnitude=%.2f durationUs=%u periodUs=%u pulseCount=%u",
            pad, magnitude, durationUs, periodUs, pulseCount);

    // Prefer the cached gamepad handle (SDL_SendGamepadEffect); fall back to
    // SDL_SendJoystickEffect if no gamepad handle was resolved at Init() time.
    // Both reach the same HIDAPI steam SendJoystickEffect handler.
    if (m_gamepad) {
        if (!SDL_SendGamepadEffect(m_gamepad, report, sizeof(report))) {
            LOG_WARN(kTag, "SendSteamControllerHaptic - SDL_SendGamepadEffect failed: %s", SDL_GetError());
        }
    } else {
        if (!SDL_SendJoystickEffect(m_joystick, report, sizeof(report))) {
            LOG_WARN(kTag, "SendSteamControllerHaptic - SDL_SendJoystickEffect fallback failed: %s", SDL_GetError());
        }
    }
}

// ==================== DualSense-Specific ====================

int GamepadHaptics::SendDualSenseTrigger(const char* trigger,
                                         const char* effectType,
                                         const std::map<std::string, int>& params) {
    return PlayDualSenseTrigger(trigger, effectType, params);
}

int GamepadHaptics::PlayDualSenseTrigger(const std::string& trigger,
                                         const std::string& effect_type,
                                         const std::map<std::string, int>& params) {
    if (!m_dualSense) {
        LOG_WARN(kTag, "SendDualSenseTrigger - Not a DualSense controller");
        return -1;
    }

    int result = m_dualSense->SetTriggerEffect(trigger, effect_type, params);
    if (result == 0) {
        std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
        auto& info        = m_activeDualSenseTriggers[trigger];
        info.effect_type  = effect_type;
        info.params       = params;
        info.last_updated = SDL_GetTicks();
    }
    return result;
}

void GamepadHaptics::SetDualSenseLED(uint8_t red, uint8_t green, uint8_t blue) {
    if (m_dualSense) {
        m_dualSense->SetLEDColor(DualSense::RGBColor(red, green, blue));
        m_dualSense->ApplyOutputState();
    }
}

// ==================== Xbox-Specific ====================

int GamepadHaptics::SendXboxImpulseTrigger(uint8_t leftIntensity,
                                           uint8_t rightIntensity,
                                           uint32_t durationMs) {
    if (!m_xbox) {
        LOG_WARN(kTag, "SendXboxImpulseTrigger - Not an Xbox controller");
        return -1;
    }

    return m_xbox->SetImpulseTriggers(leftIntensity, rightIntensity, durationMs);
}

int GamepadHaptics::PlayXboxTrigger(uint8_t left_intensity, uint8_t right_intensity, uint32_t duration_ms) {
    if (!m_xbox) {
        LOG_WARN(kTag, "PlayXboxTrigger - Not an Xbox controller");
        return -1;
    }

    int result = m_xbox->SetImpulseTriggers(left_intensity, right_intensity, static_cast<uint16_t>(duration_ms));
    if (result == 0) {
        std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
        m_activeXboxTrigger.active          = (left_intensity != 0 || right_intensity != 0);
        m_activeXboxTrigger.left_intensity  = left_intensity;
        m_activeXboxTrigger.right_intensity = right_intensity;
        m_activeXboxTrigger.duration_ms     = duration_ms;
        m_activeXboxTrigger.last_updated    = SDL_GetTicks();
    }
    return result;
}

ActiveXboxTriggerInfo GamepadHaptics::GetActiveXboxTrigger() {
    std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
    return m_activeXboxTrigger;
}