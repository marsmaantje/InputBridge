/**
 * @file GamepadHaptics.h
 * @brief Unified haptic interface for game controllers
 * 
 * This class provides a high-level interface that automatically delegates to
 * the appropriate controller-specific implementation:
 * - DualSenseController for PS5 controllers
 * - XboxController for Xbox controllers
 * - Standard rumble for everything else
 * 
 * @author InputBridge Team
 * @version 3.10
 * @date 2026-06-02
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
     * @brief Stop all active haptic effects on this gamepad.
     *
     * In addition to the base HapticDevice::StopAll() behaviour (clearing
     * rumble/constant/periodic/condition effects and their SDL effect IDs),
     * this also sends an explicit "off" adaptive-trigger effect to both
     * DualSense triggers. The base implementation only clears the in-memory
     * m_activeDualSenseTriggers tracking map for UI display purposes - it
     * never actually tells the controller hardware to release trigger
     * tension, so a DualSense would otherwise stay physically resisted
     * after InputBridge closes or a "Stop All Effects" is issued.
     * No-op on non-DualSense controllers.
     */
    void StopAll() override;

    /**
     * @brief Advertise gamepad capabilities: rumble always; adaptive triggers
     *        only when a DualSense is connected; impulse triggers only when
     *        an Xbox controller is connected.
     */
    HapticCapabilities caps() const override {
        HapticCapabilities c;
        c.rumble           = true;
        c.adaptiveTriggers = IsDualSense();
        c.impulseTriggers  = IsXboxController();
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

    /**
     * @brief HapticDevice virtual override for Xbox impulse triggers.
     *
     * Only works on Xbox controllers. Silently ignored on other controllers.
     * Delegates to SendXboxImpulseTrigger() and tracks active state for
     * network dispatch (OutputMapper/HapticDispatcher) and UI display.
     */
    int PlayXboxTrigger(uint8_t left_intensity, uint8_t right_intensity, uint32_t duration_ms) override;

    /**
     * @brief Get the currently active Xbox impulse trigger state.
     * @return ActiveXboxTriggerInfo with active=false if no effect is playing
     *         or this isn't an Xbox controller.
     */
    ActiveXboxTriggerInfo GetActiveXboxTrigger() override;

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
     * @brief Check if connected controller is a Steam Controller (V1 or V2)
     * @return true if any Steam Controller
     */
    bool IsSteamController() const;

    /**
     * @brief Check if connected controller is a Steam Controller V1 (D0G)
     *
     * V1 has no traditional rumble motors - haptic feedback requires the
     * vendor HID trackpad-pulse path via SDL_SendGamepadEffect.
     *
     * @return true if Steam Controller V1 (USB 0x1102 or wireless dongle 0x1106)
     */
    bool IsSteamControllerV1() const;

    /**
     * @brief Check if connected controller is a Steam Controller V2 (HEADCRAB, 2026)
     *
     * SDL 3.4.10 fixed rumble on the V2, so SDL_RumbleGamepad works natively.
     * The HID trackpad-pulse path is NOT used for V2.
     *
     * @return true if Steam Controller V2 (USB 0x1201 or BT 0x1202)
     */
    bool IsSteamControllerV2() const;

    /**
     * @brief Get human-readable controller name
     * @return Controller type name
     */
    const char* GetControllerTypeName() const;

private:
    /**
     * @brief Send haptic pulse to a Steam Controller trackpad via SDL_SendGamepadEffect.
     *
     * Builds a 65-byte FeatureReportMsg (ID_TRIGGER_HAPTIC_PULSE / 0x8F) and
     * dispatches it through SDL's already-open HIDAPI handle.  Works for both
     * V1 (0x1102, 0x1106) and V2 / HEADCRAB (0x1201, 0x1202) hardware.
     *
     * @param pad       Trackpad index (0 = left, 1 = right)
     * @param magnitude Pulse strength (0.0–1.0)
     * @param durationMs Duration in milliseconds
     */
    void SendSteamControllerHaptic(uint8_t pad, float magnitude, uint32_t durationMs);

    // ==================== Specialized Controllers ====================

    std::unique_ptr<DualSenseController> m_dualSense;
    std::unique_ptr<XboxController> m_xbox;

    /// Cached gamepad handle, resolved once in Init() from m_joystick.
    /// Used by PlayRumble() and SendSteamControllerHaptic() so they never
    /// need to re-resolve via SDL_GetGamepadFromID() at call time.
    SDL_Gamepad* m_gamepad = nullptr;

    // ==================== Steam Controller Constants ====================

    static constexpr uint16_t VALVE_VENDOR_ID                   = 0x28DE;
    static constexpr uint16_t STEAM_CONTROLLER_USB_PID          = 0x1102; ///< V1 wired USB (D0G)
    static constexpr uint16_t STEAM_CONTROLLER_WIRELESS_PID     = 0x1106; ///< V1 wireless dongle (D0G)
    static constexpr uint16_t STEAM_CONTROLLER_V2_USB_PID       = 0x1201; ///< V2 wired USB (HEADCRAB)
    static constexpr uint16_t STEAM_CONTROLLER_V2_BT_PID        = 0x1202; ///< V2 Bluetooth (HEADCRAB)
    // SDL 3.4.10+ (SDL_hidapi_steam_triton.c): third-generation Steam Controller
    static constexpr uint16_t STEAM_CONTROLLER_TRITON_USB_PID   = 0x1302; ///< V2 wired USB (TRITON)
    static constexpr uint16_t STEAM_CONTROLLER_TRITON_BLE_PID   = 0x1303; ///< V2 Bluetooth LE (TRITON)
    static constexpr uint16_t STEAM_CONTROLLER_PROTEUS_PID      = 0x1304; ///< V2 Proteus dongle (TRITON)
    static constexpr uint16_t STEAM_CONTROLLER_NEREID_PID       = 0x1305; ///< V2 Nereid dongle (TRITON)

    /// ID_TRIGGER_HAPTIC_PULSE command byte, per Valve controller_constants.h
    static constexpr uint8_t  STEAM_HAPTIC_PULSE_MSG_ID     = 0x8F;

    /// SDL_SendGamepadEffect requires exactly this many bytes (FeatureReportMsg size).
    static constexpr int      STEAM_FEATURE_REPORT_SIZE     = 65;

    /// Approximate duration sustained by one haptic-pulse report, used to
    /// calculate pulse_count from a caller-supplied duration in milliseconds.
    static constexpr uint32_t STEAM_HAPTIC_PULSE_DURATION_MS = 10;
};