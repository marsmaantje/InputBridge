/**
 * @file GamepadHaptics.h
 * @brief Unified haptic interface for game controllers
 *
 * This class provides a high-level interface that automatically delegates to
 * the appropriate controller-specific implementation:
 * - DualSenseController for PS5 controllers
 * - XboxController for Xbox controllers
 * - Raw HID haptic pulses for Steam Controllers (both wired and wireless)
 * - Standard SDL rumble for everything else
 *
 * @author InputBridge Team
 * @version 3.4
 * @date 2026-05-07
 */

#pragma once

#include "HapticDevice.h"
#include "DualSenseController.h"
#include "XboxController.h"
#include <cstdint>
#include <string>
#include <map>
#include <memory>

/**
 * @class GamepadHaptics
 * @brief Unified interface for all gamepad haptic feedback
 *
 * This class automatically detects the controller type and delegates to
 * the appropriate specialized implementation. It provides a simple unified
 * API regardless of controller type.
 *
 * Example Usage:
 * @code
 * GamepadHaptics haptics(joystick);
 * if (haptics.Init()) {
 *     // Works with any controller
 *     haptics.Rumble(0.5f, 0.8f, 1000);
 *
 *     // DualSense-specific (ignored on other controllers)
 *     std::map<std::string, int> params = {{"position", 5}, {"strength", 7}};
 *     haptics.SendDualSenseTrigger("right", "feedback", params);
 *
 *     // Xbox-specific (ignored on other controllers)
 *     haptics.SendXboxImpulseTrigger(0, 255, 100);
 * }
 * @endcode
 */
class GamepadHaptics : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    /**
     * @brief Initialize haptic system
     * @return Result with success/failure
     */
    InputBridge::Result<bool, InputBridge::HapticError> Init();

    /**
     * @brief Check if haptics are ready
     * @return true if ready
     */
    bool IsReady() const override;

    // ==================== Universal Rumble ====================

    /**
     * @brief Play standard dual-motor rumble
     *
     * Works on all controllers. Steam Controllers are handled via raw HID
     * haptic pulses to the trackpad actuators; all other controllers use
     * SDL_RumbleGamepad.
     *
     * @param slot Effect slot index (allows multiple simultaneous instances)
     * @param largeMagnitude Low-frequency motor (0.0-1.0)
     * @param smallMagnitude High-frequency motor (0.0-1.0)
     * @param durationMs Duration in milliseconds
     * @return 0 on success, negative on error
     */
    int PlayRumble(int slot, float largeMagnitude, float smallMagnitude, uint32_t durationMs) override;
    int PlayDualSenseTrigger(const std::string& trigger, const std::string& effect_type, const std::map<std::string, int>& params) override;

    // ==================== DualSense-Specific ====================

    /**
     * @brief Send DualSense adaptive trigger effect
     *
     * Only works on DualSense controllers. Silently ignored on other controllers.
     *
     * @param trigger "left", "right", or "both"
     * @param effectType Effect type string
     * @param params Effect-specific parameters
     * @return 0 on success, negative on error
     */
    int SendDualSenseTrigger(const char* trigger,
                            const char* effectType,
                            const std::map<std::string, int>& params);

    /**
     * @brief Set DualSense LED color
     * @param red Red component (0-255)
     * @param green Green component (0-255)
     * @param blue Blue component (0-255)
     */
    void SetDualSenseLED(uint8_t red, uint8_t green, uint8_t blue);

    // ==================== Xbox-Specific ====================

    /**
     * @brief Send Xbox impulse trigger effect
     *
     * Only works on Xbox controllers. Silently ignored on other controllers.
     *
     * @param leftIntensity Left trigger motor (0-255)
     * @param rightIntensity Right trigger motor (0-255)
     * @param durationMs Duration in milliseconds
     * @return 0 on success, negative on error
     */
    int SendXboxImpulseTrigger(uint8_t leftIntensity,
                              uint8_t rightIntensity,
                              uint32_t durationMs);

    // ==================== Controller Detection ====================

    /**
     * @brief Check if connected controller is a DualSense
     * @return true if DualSense or DualSense Edge
     */
    bool IsDualSense() const;

    /**
     * @brief Check if connected controller is an Xbox controller
     * @return true if Xbox with impulse triggers
     */
    bool IsXboxController() const;

    /**
     * @brief Check if connected controller is a Steam Controller
     *
     * Matches wired USB (0x1102) and wireless-via-dongle (0x1106) connections,
     * plus a name-based fallback for any unrecognised Valve VID variant.
     *
     * @return true if Steam Controller (any connection mode)
     */
    bool IsSteamController() const;

    /**
     * @brief Get human-readable controller name
     * @return Controller type name
     */
    const char* GetControllerTypeName() const;

private:
    /**
     * @brief Send a haptic pulse to one Steam Controller trackpad via raw HID.
     *
     * Writes a vendor-specific HID report (0x87 / msg 11) directly to the
     * joystick's HID device.  This is the only way to drive the trackpad LRA
     * actuators — SDL_RumbleGamepad has no effect on Steam Controllers because
     * they have no traditional eccentric-mass / LRA rumble motors.
     *
     * The pulse duration is approximated by repeating the HID write in a
     * fire-and-forget loop; the hardware sustains each pulse for ~5 ms, so
     * roughly (durationMs / 5) writes are issued.
     *
     * @param pad       Trackpad index: 0 = left, 1 = right
     * @param magnitude Pulse strength [0.0, 1.0]  →  mapped to [0, 0xFFFF]
     * @param durationMs Total duration in milliseconds
     */
    void SendSteamControllerHaptic(uint8_t pad, float magnitude, uint32_t durationMs);

    // ==================== Specialized Controllers ====================

    std::unique_ptr<DualSenseController> m_dualSense;
    std::unique_ptr<XboxController>      m_xbox;

    // ==================== Steam Controller Constants ====================

    static constexpr uint16_t VALVE_VENDOR_ID = 0x28DE;

    // USB Product IDs for the Steam Controller (Valve VID = 0x28DE):
    //   0x1102  —  wired USB connection
    //   0x1106  —  wireless connection via USB dongle
    // Note: 0x1101 is the dongle receiver itself, not the controller.
    static constexpr uint16_t STEAM_CONTROLLER_USB_PID      = 0x1102;
    static constexpr uint16_t STEAM_CONTROLLER_WIRELESS_PID = 0x1106;

    // HID report constants for the vendor-specific haptic pulse command.
    // Report 0x87, message type 11 — documented in Valve's open-source
    // Steam Controller firmware and confirmed in SDL's hidapi driver.
    static constexpr uint8_t STEAM_CONTROLLER_REPORT_ID  = 0x87;
    static constexpr uint8_t STEAM_HAPTIC_PULSE_MSG_ID   = 11;

    // Each HID haptic-pulse report sustains the actuator for ~5 ms.
    static constexpr uint32_t STEAM_HAPTIC_PULSE_DURATION_MS = 5;
};