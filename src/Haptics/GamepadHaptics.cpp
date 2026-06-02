/**
 * @file GamepadHaptics.cpp
 * @brief Unified haptic interface for game controllers
 *
 * Fix history:
 *   v3.7 (2026-06-02)
 *     - Fixed V1 trackpad rumble regression introduced in v3.6.  SDL 3.4.10
 *       added a separate "triton" driver for V2 (SDL_hidapi_steam_triton.c),
 *       which may cause SDL to no longer present the V1 as a gamepad-type
 *       device in some configurations, making SDL_GetGamepadFromID return null
 *       inside SendSteamControllerHaptic() and silently dropping all haptic
 *       writes.  Fixed by falling back to SDL_SendJoystickEffect (joystick
 *       path) when no gamepad handle is available — both paths reach the same
 *       HIDAPI steam SendJoystickEffect handler, so the 65-byte
 *       ID_TRIGGER_HAPTIC_PULSE report is delivered either way.
 *
 *   v3.6 (2026-06-02)
 *     - SDL 3.4.10 fixed SDL_RumbleGamepad on the Steam Controller V2 (HEADCRAB,
 *       PIDs 0x1201 / 0x1202).  The V2 now uses the standard SDL rumble path,
 *       exactly like any other gamepad.  Only V1 (0x1102, 0x1106) continues to
 *       use the vendor HID trackpad-pulse path via SDL_SendGamepadEffect, because
 *       V1 hardware has no conventional rumble motors and SDL never supported it
 *       through SDL_RumbleGamepad.
 *     - Added IsSteamControllerV1() and IsSteamControllerV2() helpers so call
 *       sites can distinguish the two generations without repeating PID lists.
 *     - PlayRumble() now branches on IsSteamControllerV1() instead of the
 *       broader IsSteamController(), so V2 controllers fall through to the
 *       standard SDL_RumbleGamepad path.
 *     - GetControllerTypeName() reports "Steam Controller V1" / "Steam Controller V2".
 *
 *   v3.5 (2026-06-02)
 *     - Added Steam Controller V2 (HEADCRAB) PIDs 0x1201 and 0x1202 to
 *       IsSteamController(), fixing the missing rumble on the new hardware.
 *     - Replaced raw SDL_hid_open / SDL_hid_write approach in
 *       SendSteamControllerHaptic() with SDL_SendGamepadEffect, using a
 *       properly-formed 65-byte FeatureReportMsg with ID_TRIGGER_HAPTIC_PULSE
 *       (0x8F).  The old code used report ID 0x87 (ID_SET_SETTINGS_VALUES),
 *       which the firmware silently ignored — this was the root cause of
 *       trackpad rumble not working on V1 hardware.  The SDL_SendGamepadEffect
 *       path also removes the need for a separate HID handle (m_steamHidDevice)
 *       and the associated open/close lifecycle in Init() / ~GamepadHaptics().
 *
 *   v3.4 (2026-05-07)
 *     - Corrected Steam Controller wireless PID: 0x1142 → 0x1106.
 *       0x1106 is what SDL sees when the controller connects via its USB dongle;
 *       0x1142 does not correspond to any shipping Valve hardware.
 *     - Implemented SendSteamControllerHaptic() via raw HID writes (report 0x87,
 *       msg 11).  Steam Controllers have no traditional rumble motors — their
 *       trackpad LRA actuators are only reachable through this vendor HID path,
 *       not through SDL_RumbleGamepad.
 *     - Routed PlayRumble() through SendSteamControllerHaptic() for Steam
 *       Controllers so both wired and wireless variants now produce haptic output.
 *
 * @author InputBridge Team
 * @version 3.7
 * @date 2026-06-02
 */

#include "GamepadHaptics.h"
#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cstring>

// ==================== Destructor ====================

GamepadHaptics::~GamepadHaptics() {
}

// ==================== Initialization ====================

InputBridge::Result<bool, InputBridge::HapticError> GamepadHaptics::Init() {
    // Initialize base haptic device
    auto result = HapticDevice::Init();

    // Create specialized controllers based on hardware
    if (IsDualSense()) {
        m_dualSense = std::make_unique<DualSenseController>(m_joystick);
        m_dualSense->Init();
        SDL_Log("GamepadHaptics: Initialized as DualSense");
    }
    else if (IsXboxController()) {
        m_xbox = std::make_unique<XboxController>(m_joystick);
        m_xbox->Init();
        SDL_Log("GamepadHaptics: Initialized as Xbox");
    }
    else if (IsSteamControllerV1()) {
        // V1 haptic pulses are sent via SDL_SendGamepadEffect in SendSteamControllerHaptic(),
        // which routes through SDL's already-open HIDAPI handle.  No separate HID open needed.
        SDL_Log("GamepadHaptics: Initialized as Steam Controller V1 (trackpad HID haptics)");
    }
    else if (IsSteamControllerV2()) {
        // V2 supports SDL_RumbleGamepad natively since SDL 3.4.10.
        // No special initialization required — the standard rumble path is used.
        SDL_Log("GamepadHaptics: Initialized as Steam Controller V2 (SDL_RumbleGamepad)");
    }
    else {
        SDL_Log("GamepadHaptics: Initialized as generic gamepad");
    }

    return result;
}

bool GamepadHaptics::IsReady() const {
    // Check base class (SDL haptic handle open)
    if (HapticDevice::IsReady()) {
        return true;
    }

    // Check if we have a gamepad handle (covers SDL rumble path)
    SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
    if (gamepad) {
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
    const Uint16 vendor = SDL_GetJoystickVendor(m_joystick);

    if (vendor != 0x045E) {  // Microsoft
        return false;
    }

    const Uint16 product = SDL_GetJoystickProduct(m_joystick);

    // Known Xbox controllers with impulse triggers
    return (product == 0x02D1 ||  // Xbox One
            product == 0x02EA ||  // Xbox One S
            product == 0x02E3 ||  // Xbox One Elite
            product == 0x0B00 ||  // Xbox One Elite 2
            product == 0x0B13);   // Xbox Series X|S
}

bool GamepadHaptics::IsSteamController() const {
    return IsSteamControllerV1() || IsSteamControllerV2();
}

bool GamepadHaptics::IsSteamControllerV1() const {
    const Uint16 vendor  = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);

    // V1 (D0G): wired USB and wireless dongle PIDs.
    // These controllers have no traditional rumble motors; haptic feedback
    // requires the vendor HID trackpad-pulse path via SDL_SendGamepadEffect.
    return (vendor == VALVE_VENDOR_ID &&
            (product == STEAM_CONTROLLER_USB_PID ||       // 0x1102 V1 wired
             product == STEAM_CONTROLLER_WIRELESS_PID));  // 0x1106 V1 wireless dongle
}

bool GamepadHaptics::IsSteamControllerV2() const {
    const Uint16 vendor  = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);

    // V2 (HEADCRAB, 2026): wired USB and Bluetooth PIDs.
    // SDL 3.4.10 fixed SDL_RumbleGamepad for these, so the standard rumble
    // path works — no need for the custom HID haptic-pulse approach.
    if (vendor == VALVE_VENDOR_ID &&
        (product == STEAM_CONTROLLER_V2_USB_PID ||  // 0x1201 V2 wired
         product == STEAM_CONTROLLER_V2_BT_PID)) {  // 0x1202 V2 Bluetooth
        return true;
    }

    // Name-based fallback for any Valve V2 variant not covered by the PIDs above.
    // IsSteamControllerV1() already matched 0x1102/0x1106, so anything that
    // still reaches here and carries "Steam Controller" in its name is treated
    // as V2 (i.e. uses the SDL_RumbleGamepad path).
    const char* name = SDL_GetJoystickName(m_joystick);
    if (name && std::strstr(name, "Steam Controller") && !IsSteamControllerV1()) {
        return true;
    }

    return false;
}

const char* GamepadHaptics::GetControllerTypeName() const {
    if (IsDualSense())          return "DualSense";
    if (IsXboxController())     return "Xbox";
    if (IsSteamControllerV1())  return "Steam Controller V1";
    if (IsSteamControllerV2())  return "Steam Controller V2";
    return "Generic Gamepad";
}

// ==================== Universal Rumble ====================

int GamepadHaptics::PlayRumble(int slot, float largeMagnitude, float smallMagnitude, uint32_t durationMs) {
    largeMagnitude = std::clamp(largeMagnitude, 0.0f, 1.0f);
    smallMagnitude = std::clamp(smallMagnitude, 0.0f, 1.0f);

    RunAsync([this, slot, largeMagnitude, smallMagnitude, durationMs]() {

        // ── Steam Controller V1 path ──────────────────────────────────────────
        // V1 (D0G, PIDs 0x1102 / 0x1106) has no traditional eccentric-mass or
        // LRA rumble motors.  SDL_RumbleGamepad does nothing on it.  The only
        // way to produce haptic feedback is to write vendor HID reports directly
        // to both trackpad actuators via SDL_SendGamepadEffect.
        //
        // V2 (HEADCRAB, PIDs 0x1201 / 0x1202) has real rumble motors and SDL
        // 3.4.10 fixed SDL_RumbleGamepad support for it, so it falls through to
        // the standard path below.
        //
        // We use the average of the two magnitudes as the pulse strength, then
        // fire both pads simultaneously to approximate a symmetric "rumble" feel.
        if (IsSteamControllerV1()) {
            const float magnitude = (largeMagnitude + smallMagnitude) * 0.5f;
            SDL_Log("GamepadHaptics::PlayRumble - Steam Controller V1: HID trackpad haptic (magnitude=%.2f, duration=%ums)",
                    magnitude, durationMs);

            SendSteamControllerHaptic(0, magnitude, durationMs); // left  trackpad
            SendSteamControllerHaptic(1, magnitude, durationMs); // right trackpad

            if (magnitude > 0.0f) {
                std::lock_guard<std::mutex> lock(m_activeEffectsMutex);
                auto& info          = m_activeRumbles[slot];
                info.active         = true;
                info.large_magnitude = largeMagnitude;
                info.small_magnitude = smallMagnitude;
                info.duration_ms    = durationMs;
                info.last_updated   = SDL_GetTicks();
            }
            return;
        }

        // ── Standard SDL path (all other gamepads) ────────────────────────────
        SDL_JoystickID id      = SDL_GetJoystickID(m_joystick);
        SDL_Gamepad*   gamepad = SDL_GetGamepadFromID(id);

        if (gamepad) {
            const Uint16 lowFreq  = static_cast<Uint16>(largeMagnitude * 0xFFFF);
            const Uint16 highFreq = static_cast<Uint16>(smallMagnitude * 0xFFFF);

            if (!SDL_RumbleGamepad(gamepad, lowFreq, highFreq, durationMs)) {
                SDL_Log("GamepadHaptics::PlayRumble - SDL_RumbleGamepad failed: %s", SDL_GetError());
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
                SDL_Log("GamepadHaptics::PlayRumble - Success slot=%d (low=%u, high=%u, duration=%ums)",
                        slot, lowFreq, highFreq, durationMs);
            }
        } else {
            SDL_Log("GamepadHaptics::PlayRumble - No gamepad handle available");
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
 * exactly 65-byte payloads — it calls SDL_hid_send_feature_report on its
 * already-open device handle.  We build a FeatureReportMsg (from Valve's
 * controller_structs.h) with command ID_TRIGGER_HAPTIC_PULSE (0x8F):
 *
 *   Byte  0   : reserved / report ID (0x00 — SDL prepends this)
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
    report[1] = STEAM_HAPTIC_PULSE_MSG_ID;   // 0x8F — ID_TRIGGER_HAPTIC_PULSE
    report[2] = 9;                           // payload length = sizeof(MsgFireHapticPulse)
    report[3] = pad;                         // which_pad: 0 = left, 1 = right
    report[4] = static_cast<uint8_t>(durationUs & 0xFF);          // pulse_duration lo
    report[5] = static_cast<uint8_t>((durationUs >> 8) & 0xFF);   // pulse_duration hi
    report[6] = static_cast<uint8_t>(periodUs & 0xFF);            // pulse_interval lo
    report[7] = static_cast<uint8_t>((periodUs >> 8) & 0xFF);     // pulse_interval hi
    report[8] = static_cast<uint8_t>(pulseCount & 0xFF);          // pulse_count lo
    report[9] = static_cast<uint8_t>((pulseCount >> 8) & 0xFF);   // pulse_count hi
    // report[10–11] = dBgain (Sint16) — 0 = default gain
    // report[12]    = priority flags — 0 = HAPTIC_PULSE_NORMAL

    SDL_Log("SendSteamControllerHaptic - pad=%u magnitude=%.2f durationUs=%u periodUs=%u pulseCount=%u",
            pad, magnitude, durationUs, periodUs, pulseCount);

    // SDL_SendGamepadEffect routes through the HIDAPI steam driver's SendJoystickEffect,
    // which calls SDL_hid_send_feature_report on its already-open handle.
    // This is the preferred path when the V1 is opened as a gamepad (SDL_JOYSTICK_TYPE_GAMEPAD).
    //
    // However, SDL 3.4.10 introduced a separate "triton" driver for V2, which may affect
    // how SDL classifies V1 devices in some configurations. If SDL_GetGamepadFromID returns
    // null (V1 not opened as a gamepad), we fall back to SDL_SendJoystickEffect, which
    // calls the same underlying HIDAPI steam SendJoystickEffect handler via the joystick path.
    SDL_JoystickID id      = SDL_GetJoystickID(m_joystick);
    SDL_Gamepad*   gamepad = SDL_GetGamepadFromID(id);
    if (gamepad) {
        if (!SDL_SendGamepadEffect(gamepad, report, sizeof(report))) {
            SDL_Log("SendSteamControllerHaptic - SDL_SendGamepadEffect failed: %s", SDL_GetError());
        }
    } else {
        // Fallback: send via the joystick handle directly.
        // SDL_SendJoystickEffect hits the same HIDAPI steam SendJoystickEffect handler.
        if (!SDL_SendJoystickEffect(m_joystick, report, sizeof(report))) {
            SDL_Log("SendSteamControllerHaptic - SDL_SendJoystickEffect fallback failed: %s", SDL_GetError());
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
        SDL_Log("GamepadHaptics::SendDualSenseTrigger - Not a DualSense controller");
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
        SDL_Log("GamepadHaptics::SendXboxImpulseTrigger - Not an Xbox controller");
        return -1;
    }

    return m_xbox->SetImpulseTriggers(leftIntensity, rightIntensity, durationMs);
}