/**
 * @file DualSenseController.h
 * @brief Sony DualSense (PS5) controller implementation with adaptive trigger support
 * 
 * This class provides complete DualSense controller functionality including:
 * - Adaptive trigger effects (all 7 official effect types)
 * - USB and Bluetooth protocol support with automatic detection
 * - LED control, rumble motors, and player indicators
 * - Proper HID report formatting for both connection types
 * 
 * @author InputBridge Team
 * @version 2.0
 * @date 2026-02-14
 */

#pragma once

#include "HapticDevice.h"
#include <SDL3/SDL_joystick.h>
#include <cstdint>
#include <array>
#include <map>
#include <string>

/**
 * @namespace DualSense
 * @brief DualSense controller constants and types
 */
namespace DualSense {
    /**
     * @enum ConnectionType
     * @brief DualSense connection method
     */
    enum class ConnectionType {
        Unknown,
        USB,
        Bluetooth
    };

    /**
     * @struct RGBColor
     * @brief RGB color for LED control
     */
    struct RGBColor {
        uint8_t red;
        uint8_t green;
        uint8_t blue;

        RGBColor() : red(0), green(0), blue(0) {}
        RGBColor(uint8_t r, uint8_t g, uint8_t b) : red(r), green(g), blue(b) {}
    };

    /**
     * @struct OutputState
     * @brief Complete output state for DualSense controller
     */
    struct OutputState {
        // Rumble motors
        uint8_t rightRumble;      ///< Right rumble motor (0-255)
        uint8_t leftRumble;       ///< Left rumble motor (0-255)

        // Trigger effects (11 bytes each)
        std::array<uint8_t, 11> rightTriggerEffect;
        std::array<uint8_t, 11> leftTriggerEffect;

        // LED control
        RGBColor ledColor;
        uint8_t ledBrightness;
        uint8_t playerLEDs;       ///< Player indicator LEDs (5-bit mask)

        // Mute button LED
        uint8_t muteLED;          ///< Mute button LED state

        OutputState();
        void Reset();
    };
}

/**
 * @class DualSenseController
 * @brief Complete DualSense controller implementation
 * 
 * This class handles all DualSense-specific functionality including:
 * - Automatic USB vs Bluetooth detection
 * - Proper HID report formatting for each connection type
 * - Adaptive trigger effect generation and transmission
 * - LED and rumble motor control
 * - State management
 * 
 * Thread Safety: All operations use async execution through base HapticDevice
 * 
 * Example Usage:
 * @code
 * DualSenseController controller(joystick);
 * if (controller.Init()) {
 *     // Set trigger effect
 *     std::map<std::string, int> params = {{"position", 5}, {"strength", 7}};
 *     controller.SetTriggerEffect("right", "feedback", params);
 *     
 *     // Set LED color
 *     controller.SetLEDColor(DualSense::RGBColor(255, 0, 0)); // Red
 *     
 *     // Apply all changes
 *     controller.ApplyOutputState();
 * }
 * @endcode
 */
class DualSenseController : public HapticDevice {
public:
    using HapticDevice::HapticDevice;

    /**
     * @brief Initialize DualSense controller
     * @return Result with success/failure and error info
     */
    InputBridge::Result<bool, InputBridge::HapticError> Init();

    /**
     * @brief Check if controller is ready for operations
     * @return true if ready, false otherwise
     */
    bool IsReady() const override;

    // ==================== Trigger Effects ====================

    /**
     * @brief Set adaptive trigger effect
     * 
     * Available effects:
     * - "off": Disable trigger effect
     * - "feedback": Constant resistance from position
     * - "weapon": Two-stage trigger (gun simulation)
     * - "vibration": Vibration at trigger position
     * - "bow": Bow tension and release
     * - "galloping": Rhythmic resistance pattern
     * - "machine": Complex vibration pattern
     * 
     * @param trigger "left", "right", or "both"
     * @param effectType Effect type string
     * @param params Effect-specific parameters
     * @return 0 on success, negative on error
     */
    int SetTriggerEffect(const std::string& trigger, 
                        const std::string& effectType,
                        const std::map<std::string, int>& params);

    /**
     * @brief Disable all trigger effects
     */
    void DisableTriggerEffects();

    // ==================== LED Control ====================

    /**
     * @brief Set controller LED color
     * @param color RGB color
     */
    void SetLEDColor(const DualSense::RGBColor& color);

    /**
     * @brief Set LED brightness
     * @param brightness Brightness level (0-255)
     */
    void SetLEDBrightness(uint8_t brightness);

    /**
     * @brief Set player indicator LEDs
     * @param playerMask 5-bit mask for player LEDs
     */
    void SetPlayerLEDs(uint8_t playerMask);

    /**
     * @brief Set mute button LED
     * @param state LED state (0=off, 1=on, 2=pulse)
     */
    void SetMuteLED(uint8_t state);

    // ==================== Rumble Motors ====================

    /**
     * @brief Set rumble motors
     * @param leftIntensity Left motor intensity (0-255)
     * @param rightIntensity Right motor intensity (0-255)
     */
    void SetRumble(uint8_t leftIntensity, uint8_t rightIntensity);

    // ==================== State Management ====================

    /**
     * @brief Apply current output state to controller
     * 
     * Sends the accumulated state (triggers, LEDs, rumble) to the controller
     * using the appropriate USB or Bluetooth protocol.
     */
    void ApplyOutputState();

    /**
     * @brief Get connection type
     * @return Current connection type (USB or Bluetooth)
     */
    DualSense::ConnectionType GetConnectionType() const;

    /**
     * @brief Check if this is a DualSense controller
     * @return true if DualSense or DualSense Edge
     */
    bool IsDualSense() const;

private:
    // ==================== Protocol Implementation ====================

    /**
     * @brief Detect USB vs Bluetooth connection
     * @return Connection type
     */
    DualSense::ConnectionType DetectConnectionType() const;

    /**
     * @brief Send output state via USB protocol
     * @return true on success
     */
    bool SendUSBOutput();

    /**
     * @brief Send output state via Bluetooth protocol
     * @return true on success
     */
    bool SendBluetoothOutput();

    /**
     * @brief Apply trigger effect to trigger data array
     * @param triggerData Output array for trigger effect (11 bytes)
     * @param effectType Effect type string
     * @param params Effect parameters
     * @return true on success
     */
    bool ApplyTriggerEffect(uint8_t* triggerData,
                           const std::string& effectType,
                           const std::map<std::string, int>& params);

    // ==================== State ====================
    
    DualSense::OutputState m_outputState;
    DualSense::ConnectionType m_connectionType;
    mutable bool m_connectionTypeDetected;
    uint8_t m_bluetoothSequence;  ///< Bluetooth sequence counter

    // ==================== Protocol Constants ====================

    // Hardware IDs
    static constexpr uint16_t SONY_VENDOR_ID = 0x054C;
    static constexpr uint16_t DUALSENSE_PRODUCT_ID = 0x0CE6;
    static constexpr uint16_t DUALSENSE_EDGE_PRODUCT_ID = 0x0DF2;

    // USB Protocol
    static constexpr uint8_t USB_REPORT_ID = 0x02;
    static constexpr size_t USB_REPORT_SIZE = 63;
    static constexpr size_t USB_RIGHT_TRIGGER_OFFSET = 11;
    static constexpr size_t USB_LEFT_TRIGGER_OFFSET = 22;

    // Bluetooth Protocol
    static constexpr uint8_t BT_REPORT_ID = 0x31;
    static constexpr size_t BT_REPORT_SIZE = 78;
    static constexpr size_t BT_RIGHT_TRIGGER_OFFSET = 22;
    static constexpr size_t BT_LEFT_TRIGGER_OFFSET = 33;

    // Feature flags (USB byte 1)
    static constexpr uint8_t USB_FLAG_ENABLE_HID = 0x01;
    static constexpr uint8_t USB_FLAG_ENABLE_RUMBLE = 0x02;
    static constexpr uint8_t USB_FLAG_ENABLE_HAPTICS = 0x04;
    static constexpr uint8_t USB_FLAG_USE_RUMBLE_NOT_HAPTICS = 0x08;

    // Feature flags (BT byte 1)
    static constexpr uint8_t BT_FLAG_ENABLE_RUMBLE_EMULATION = 0x01;
    static constexpr uint8_t BT_FLAG_USE_RUMBLE_NOT_HAPTICS = 0x02;

    // Feature flags byte 2 (both USB and BT)
    static constexpr uint8_t FLAG2_ENABLE_LED_COLOR = 0x04;
    static constexpr uint8_t FLAG2_ENABLE_PLAYER_LEDS = 0x10;
    static constexpr uint8_t FLAG2_ENABLE_HAPTICS = 0x01;
    static constexpr uint8_t FLAG2_ENABLE_LIGHTBAR = 0x02;
};
