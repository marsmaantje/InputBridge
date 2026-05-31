/**
 * @file GamepadHaptics.h (Refactored)
 * @brief Unified haptic interface for game controllers
 * 
 * This class provides a high-level interface that automatically delegates to
 * the appropriate controller-specific implementation:
 * - DualSenseController for PS5 controllers
 * - XboxController for Xbox controllers
 * - Standard rumble for everything else
 * 
 * @author InputBridge Team
 * @version 3.0
 * @date 2026-02-14
 */

#pragma once

#include "HapticDevice.h"
#include "DualSenseController.h"
#include "XboxController.h"
#include <SDL3/SDL_hidapi.h>
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

    ~GamepadHaptics();

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

    /**
     * @brief Advertise gamepad capabilities: rumble always; adaptive triggers
     *        only when a DualSense is connected.
     */
    HapticCapabilities caps() const override {
        HapticCapabilities c;
        c.rumble           = true;
        c.adaptiveTriggers = IsDualSense();
        return c;
    }

    // ==================== Universal Rumble ====================

    /**
     * @brief Play standard dual-motor rumble
     * 
     * Works on all controllers. For controllers with advanced haptics,
     * this uses the main rumble motors.
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
     * @return true if Steam Controller
     */
    bool IsSteamController() const;

    /**
     * @brief Get human-readable controller name
     * @return Controller type name
     */
    const char* GetControllerTypeName() const;

private:
    /**
     * @brief Send haptic pulse to Steam Controller trackpad
     * @param pad Trackpad index (0=left, 1=right)
     * @param magnitude Pulse strength (0.0-1.0)
     * @param durationMs Duration in milliseconds
     */
    void SendSteamControllerHaptic(uint8_t pad, float magnitude, uint32_t durationMs);

    // ==================== Specialized Controllers ====================

    std::unique_ptr<DualSenseController> m_dualSense;
    std::unique_ptr<XboxController> m_xbox;

    /// Raw HID handle for Steam Controller haptic writes.
    /// Opened once in Init() and closed in the destructor.
    /// Null for all non-Steam-Controller devices.
    SDL_hid_device* m_steamHidDevice = nullptr;

    // ==================== Steam Controller Constants ====================

    static constexpr uint16_t VALVE_VENDOR_ID              = 0x28DE;
    static constexpr uint16_t STEAM_CONTROLLER_USB_PID      = 0x1102; ///< Wired USB
    static constexpr uint16_t STEAM_CONTROLLER_WIRELESS_PID = 0x1106; ///< Wireless dongle (was wrongly 0x1142 in v3.3)

    static constexpr uint8_t  STEAM_CONTROLLER_REPORT_ID    = 0x87;
    static constexpr uint8_t  STEAM_HAPTIC_PULSE_MSG_ID     = 11;

    /// Each raw HID haptic-pulse report sustains vibration for this many ms.
    /// Used by SendSteamControllerHaptic() to calculate how many writes are
    /// needed to fill the caller's requested duration.
    static constexpr uint32_t STEAM_HAPTIC_PULSE_DURATION_MS = 10;
};