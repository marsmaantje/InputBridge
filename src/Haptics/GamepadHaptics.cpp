/**
 * @file GamepadHaptics.cpp
 * @brief Unified haptic interface for game controllers
 *
 * Fix history:
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
 * @version 3.4
 * @date 2026-05-07
 */

#include "GamepadHaptics.h"
#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

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
    else if (IsSteamController()) {
        SDL_Log("GamepadHaptics: Initialized as Steam Controller");
        SDL_Log("  Haptic output via raw HID trackpad pulses (report 0x87)");
        SDL_Log("  SDL_RumbleGamepad is NOT used — no traditional motors present");
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
    const Uint16 vendor  = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);

    if (vendor == VALVE_VENDOR_ID &&
        (product == STEAM_CONTROLLER_USB_PID ||       // wired USB
         product == STEAM_CONTROLLER_WIRELESS_PID)) { // wireless via dongle (was wrongly 0x1142)
        return true;
    }

    // Name-based fallback for any Valve variant not covered by the PIDs above.
    const char* name = SDL_GetJoystickName(m_joystick);
    if (name && std::strstr(name, "Steam Controller")) {
        return true;
    }

    return false;
}

const char* GamepadHaptics::GetControllerTypeName() const {
    if (IsDualSense())        return "DualSense";
    if (IsXboxController())   return "Xbox";
    if (IsSteamController())  return "Steam Controller";
    return "Generic Gamepad";
}

// ==================== Universal Rumble ====================

int GamepadHaptics::PlayRumble(int slot, float largeMagnitude, float smallMagnitude, uint32_t durationMs) {
    largeMagnitude = std::clamp(largeMagnitude, 0.0f, 1.0f);
    smallMagnitude = std::clamp(smallMagnitude, 0.0f, 1.0f);

    RunAsync([this, slot, largeMagnitude, smallMagnitude, durationMs]() {

        // ── Steam Controller path ─────────────────────────────────────────────
        // The Steam Controller has no traditional eccentric-mass or LRA rumble
        // motors.  SDL_RumbleGamepad does nothing on it.  The only way to
        // produce haptic feedback is to write vendor HID reports directly to
        // both trackpad actuators.
        //
        // We use the average of the two magnitudes as the pulse strength, then
        // fire both pads simultaneously to approximate symmetric "rumble" feel.
        if (IsSteamController()) {
            const float magnitude = (largeMagnitude + smallMagnitude) * 0.5f;
            SDL_Log("GamepadHaptics::PlayRumble - Steam Controller: raw HID haptic (magnitude=%.2f, duration=%ums)",
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
 * Valve Steam Controller vendor HID haptic-pulse report layout (report 0x87):
 *
 *   Byte  0   : Report ID  = 0x87
 *   Byte  1   : Message length (number of payload bytes that follow) = 7
 *   Byte  2   : Message type = 11  (STEAM_HAPTIC_PULSE_MSG_ID)
 *   Byte  3   : Pad select: 0 = left trackpad, 1 = right trackpad
 *   Bytes 4–5 : Pulse duration in microseconds (little-endian Uint16)
 *               How long the actuator is driven per cycle.
 *   Bytes 6–7 : Pulse period in microseconds (little-endian Uint16)
 *               Time between the start of consecutive pulses (must be ≥ duration).
 *               A period equal to the duration gives maximum continuous vibration.
 *   Byte  8   : Repeat count (0 = play once, use 1 for a single timed burst)
 *
 * The hardware sustains one report's worth of pulses for approximately
 * STEAM_HAPTIC_PULSE_DURATION_MS milliseconds.  To fill a longer requested
 * duration we re-send the report in a blocking loop with a short sleep between
 * writes so we do not flood the USB HID endpoint.
 *
 * Reference: Valve's open-source Steam Controller firmware and SDL's
 *            hidapi/SDL_hidapi_steam.c driver.
 */
void GamepadHaptics::SendSteamControllerHaptic(uint8_t pad, float magnitude, uint32_t durationMs) {
    if (!m_joystick) {
        SDL_Log("SendSteamControllerHaptic - joystick handle is null");
        return;
    }

    magnitude = std::clamp(magnitude, 0.0f, 1.0f);

    // Map [0, 1] magnitude to a pulse duration in microseconds.
    // At magnitude 1.0 the pulse fills its entire period (max vibration).
    // At magnitude 0.0 we skip the write entirely.
    if (magnitude <= 0.0f) {
        return;
    }

    // Period is fixed at 5000 µs (5 ms).  Duration scales with magnitude so
    // lower values produce shorter on-time within each period (duty cycle).
    const Uint16 periodUs   = 5000;
    const Uint16 durationUs = static_cast<Uint16>(magnitude * static_cast<float>(periodUs));

    // Build the 9-byte HID output report.
    uint8_t report[9];
    report[0] = STEAM_CONTROLLER_REPORT_ID;  // 0x87
    report[1] = 7;                           // payload length
    report[2] = STEAM_HAPTIC_PULSE_MSG_ID;   // 11
    report[3] = pad;                         // 0 = left, 1 = right
    report[4] = static_cast<uint8_t>(durationUs & 0xFF);         // duration lo
    report[5] = static_cast<uint8_t>((durationUs >> 8) & 0xFF);  // duration hi
    report[6] = static_cast<uint8_t>(periodUs & 0xFF);           // period lo
    report[7] = static_cast<uint8_t>((periodUs >> 8) & 0xFF);    // period hi
    report[8] = 1;                           // repeat count

    // Determine how many HID writes are needed to cover the requested duration.
    // Each write sustains haptics for STEAM_HAPTIC_PULSE_DURATION_MS ms.
    const uint32_t iterations = std::max(1u,
        (durationMs + STEAM_HAPTIC_PULSE_DURATION_MS - 1) / STEAM_HAPTIC_PULSE_DURATION_MS);

    SDL_Log("SendSteamControllerHaptic - pad=%u magnitude=%.2f durationUs=%u periodUs=%u iterations=%u",
            pad, magnitude, durationUs, periodUs, iterations);

    for (uint32_t i = 0; i < iterations; ++i) {
        if (!SDL_SendJoystickEffect(m_joystick, report, static_cast<int>(sizeof(report)))) {
            SDL_Log("SendSteamControllerHaptic - SDL_SendJoystickEffect failed (iter %u): %s",
                    i, SDL_GetError());
            break;
        }
        if (i + 1 < iterations) {
            // Sleep for one pulse period before the next write.
            std::this_thread::sleep_for(
                std::chrono::milliseconds(STEAM_HAPTIC_PULSE_DURATION_MS));
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