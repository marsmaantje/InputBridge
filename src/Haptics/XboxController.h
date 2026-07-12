/**
 * @file XboxController.h
 * @brief Microsoft Xbox One/Series controller with impulse trigger support
 * 
 * Xbox One and Xbox Series X|S controllers feature independent motors in each trigger,
 * allowing for vibration feedback during gameplay.
 * 
 * @author InputBridge Team
 * @version 2.0
 * @date 2026-02-14
 */

#pragma once

#include "HapticDevice.h"
#include <SDL3/SDL_joystick.h>
#include <cstdint>

/**
 * @namespace Xbox
 * @brief Xbox controller constants and types
 */
namespace Xbox {
    /**
     * @enum ControllerModel
     * @brief Xbox controller model
     */
    enum class ControllerModel {
        Unknown,
        XboxOne,
        XboxOneS,
        XboxOneElite,
        XboxOneElite2,
        XboxSeriesX
    };

    /**
     * @struct ImpulseTriggerState
     * @brief Impulse trigger motor state
     */
    struct ImpulseTriggerState {
        uint8_t leftTriggerMotor;   ///< Left trigger motor (0-255)
        uint8_t rightTriggerMotor;  ///< Right trigger motor (0-255)
        uint16_t durationMs;        ///< Duration in milliseconds

        ImpulseTriggerState() 
            : leftTriggerMotor(0)
            , rightTriggerMotor(0)
            , durationMs(0) {}
    };
}

/**
 * @class XboxController
 * @brief Xbox controller with impulse trigger support
 * 
 * This class provides impulse trigger functionality for Xbox controllers.
 * The impulse triggers have independent motors that can vibrate during gameplay
 * to provide enhanced feedback (e.g., weapon recoil, vehicle vibration).
 * 
 * Supported Controllers:
 * - Xbox One (Model 1537)
 * - Xbox One S (Model 1708)
 * - Xbox One Elite Series 1 & 2
 * - Xbox Series X|S
 * 
 * Example Usage:
 * @code
 * XboxController controller(joystick);
 * if (controller.Init()) {
 *     // Vibrate right trigger for weapon fire
 *     controller.SetImpulseTriggers(0, 255, 100);
 *     
 *     // Vibrate both triggers for vehicle rumble
 *     controller.SetImpulseTriggers(128, 128, 1000);
 * }
 * @endcode
 */
class XboxController : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    /**
     * @brief Check if controller is ready
     * @return true if ready, false otherwise
     */
    bool IsReady() const override;

    /**
     * @brief Set impulse trigger motors
     * 
     * @param leftIntensity Left trigger motor intensity (0-255)
     * @param rightIntensity Right trigger motor intensity (0-255)
     * @param durationMs Duration in milliseconds
     * @return 0 on success, negative on error
     */
    int SetImpulseTriggers(uint8_t leftIntensity, uint8_t rightIntensity, uint16_t durationMs);

    /**
     * @brief Stop impulse trigger motors
     */
    void StopImpulseTriggers();

    /**
     * @brief Get Xbox controller model
     * @return Controller model
     */
    Xbox::ControllerModel GetModel() const;

    /**
     * @brief Check if this is an Xbox controller
     * @return true if Xbox controller with impulse triggers
     */
    bool IsXboxController() const;

    /**
     * @brief Check if a joystick handle is an Xbox controller with impulse triggers
     *
     * Static entry point for the same vendor/product-ID detection used by the
     * instance method above. This is the single source of truth for "is this
     * an Xbox controller" - callers that don't (yet) own an XboxController
     * instance, such as GamepadHaptics deciding whether to construct one,
     * should call this instead of re-implementing the product ID list.
     *
     * @param joystick SDL joystick handle to check
     * @return true if Xbox controller with impulse triggers
     */
    static bool IsXboxController(SDL_Joystick* joystick);

private:
    /**
     * @brief Detect specific Xbox controller model
     * @return Controller model
     */
    Xbox::ControllerModel DetectModel() const;

    /**
     * @brief Send impulse trigger command
     * @param state Trigger state
     * @return true on success
     */
    bool SendImpulseTriggerCommand(const Xbox::ImpulseTriggerState& state);

    // ==================== Protocol Constants ====================

    // Hardware IDs
    static constexpr uint16_t MICROSOFT_VENDOR_ID = 0x045E;
    
    // Product IDs (controllers with impulse triggers)
    //
    // Sourced against SDL upstream's authoritative list
    // (src/joystick/usb_ids.h) to make sure every connection variant of
    // each impulse-trigger-capable model is covered. Deliberately excluded:
    //  - 0x02FF (XBOXGIP_CONTROLLER): a driver-software PID, not a distinct
    //    hardware model, so it isn't a meaningful entry in a model table.
    //  - Xbox Adaptive Controller (0x0B0A / 0x0B0C / 0x0B21): a hub for
    //    external accessibility switches, has no trigger motors of its own.
    static constexpr uint16_t XBOX_ONE_PRODUCT_ID = 0x02D1;        // Original Xbox One - USB
    static constexpr uint16_t XBOX_ONE_2015FW_PRODUCT_ID = 0x02DD; // Original Xbox One - USB (2015 firmware revision)
    static constexpr uint16_t XBOX_ONE_ELITE_PRODUCT_ID = 0x02E3;  // Xbox One Elite - USB

    // Xbox One S reports a different product ID per connection type/firmware.
    static constexpr uint16_t XBOX_ONE_S_PRODUCT_ID = 0x02EA;             // Xbox One S - USB
    static constexpr uint16_t XBOX_ONE_S_BT_REV1_PRODUCT_ID = 0x02E0;     // Xbox One S - Bluetooth (older firmware)
    static constexpr uint16_t XBOX_ONE_S_BT_REV2_PRODUCT_ID = 0x02FD;     // Xbox One S - Bluetooth (newer firmware)
    static constexpr uint16_t XBOX_ONE_S_BLE_PRODUCT_ID = 0x0B20;         // Xbox One S - Bluetooth LE

    // Xbox One Elite Series 2 likewise has separate wired/BT/BLE IDs.
    static constexpr uint16_t XBOX_ONE_ELITE2_PRODUCT_ID = 0x0B00;        // Xbox One Elite 2 - USB
    static constexpr uint16_t XBOX_ONE_ELITE2_BT_PRODUCT_ID = 0x0B05;     // Xbox One Elite 2 - Bluetooth
    static constexpr uint16_t XBOX_ONE_ELITE2_BLE_PRODUCT_ID = 0x0B22;    // Xbox One Elite 2 - Bluetooth LE
    // NOTE: 0x0B22 was previously (incorrectly) listed here as a Series X|S
    // BLE id. Per SDL's usb_ids.h it's actually
    // USB_PRODUCT_XBOX_ONE_ELITE_SERIES_2_BLE - Series X|S has no separate
    // BLE-only PID; see XBOX_SERIES_X_PRODUCT_ID below.

    // Xbox Series X|S (model 1914) reports a different product ID per
    // connection type - USB and Bluetooth are NOT the same device ID here.
    // Unlike Xbox One S/Elite 2, Series X|S was never given a distinct
    // BLE-only PID: 0x0B13 covers both classic Bluetooth and BLE.
    static constexpr uint16_t XBOX_SERIES_X_PRODUCT_ID = 0x0B13;      // Xbox Series X|S - Bluetooth/BLE
    static constexpr uint16_t XBOX_SERIES_X_WIRED_PRODUCT_ID = 0x0B12; // Xbox Series X|S - USB (wired)


    // Impulse Trigger Protocol
    static constexpr uint8_t IMPULSE_TRIGGER_REPORT_ID = 0x03;
    static constexpr size_t IMPULSE_TRIGGER_REPORT_SIZE = 8;
};