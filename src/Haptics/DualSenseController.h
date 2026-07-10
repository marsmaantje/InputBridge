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
#include <mutex>
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
     * - "slope_feedback": Linearly interpolated resistance between two positions
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
     *
     * Informational only (used for logging/UI) - SDL_SendJoystickEffect's
     * underlying HIDAPI driver auto-detects and handles the USB vs Bluetooth
     * report framing itself, so this class does not need to branch on it
     * when building the effects payload.
     */
    DualSense::ConnectionType DetectConnectionType() const;

    /**
     * @brief Build and send the DualSense "effects" output payload
     *
     * IMPORTANT: SDL's PS5 HIDAPI driver (SDL_hidapi_ps5.c) builds the raw
     * HID report itself - it prepends the report ID (and, on Bluetooth, the
     * sequence/tag byte and trailing CRC) around whatever buffer is passed to
     * SDL_SendJoystickEffect(). The buffer passed here must therefore contain
     * ONLY the "DS5EffectsState_t"-equivalent payload starting at
     * ucEnableBits1 - it must NOT include a report ID byte, or every field
     * ends up shifted by one (or more) bytes once SDL adds its own header.
     * @return true on success
     */
    bool SendOutput();

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
    // Guards m_outputState: writers (SetTriggerEffect, SetRumble, SetLED*,
    // SetMuteLED, SetPlayerLEDs, DisableTriggerEffects) can run on whatever
    // thread calls them (OSC/WebSocket/UI), while SendOutput() reads it from
    // the async worker thread queued via RunAsync(). Without this, a read
    // and a write can interleave mid-struct, producing a torn HID report.
    mutable std::mutex m_outputStateMutex;
    DualSense::ConnectionType m_connectionType;
    mutable bool m_connectionTypeDetected;

    // ==================== Protocol Constants ====================

    // Hardware IDs
    static constexpr uint16_t SONY_VENDOR_ID = 0x054C;
    static constexpr uint16_t DUALSENSE_PRODUCT_ID = 0x0CE6;
    static constexpr uint16_t DUALSENSE_EDGE_PRODUCT_ID = 0x0DF2;

    // ==================== Effects Payload Layout ====================
    // Mirrors SDL's DS5EffectsState_t (src/joystick/hidapi/SDL_hidapi_ps5.c),
    // and Sony DualSense/Data_Structures' SetStateData - NOT including any
    // report ID byte, which SDL adds on our behalf.
    static constexpr size_t EFFECTS_PAYLOAD_SIZE = 47;
    static constexpr size_t RIGHT_TRIGGER_OFFSET = 10;   // rgucRightTriggerEffect[11]
    static constexpr size_t LEFT_TRIGGER_OFFSET = 21;    // rgucLeftTriggerEffect[11]
    static constexpr size_t LED_BRIGHTNESS_OFFSET = 42;  // ucLedBrightness
    static constexpr size_t PAD_LIGHTS_OFFSET = 43;      // ucPadLights (player LEDs)
    static constexpr size_t LED_RED_OFFSET = 44;         // ucLedRed
    static constexpr size_t LED_GREEN_OFFSET = 45;       // ucLedGreen
    static constexpr size_t LED_BLUE_OFFSET = 46;        // ucLedBlue

    // ucEnableBits1 (payload offset 0)
    static constexpr uint8_t ENABLE1_RUMBLE_EMULATION = 0x01;
    static constexpr uint8_t ENABLE1_DISABLE_AUDIO_HAPTICS = 0x02;
    static constexpr uint8_t ENABLE1_MODIFY_RIGHT_TRIGGER = 0x04; ///< Required for right adaptive trigger effect to take effect
    static constexpr uint8_t ENABLE1_MODIFY_LEFT_TRIGGER = 0x08;  ///< Required for left adaptive trigger effect to take effect

    // ucEnableBits2 (payload offset 1)
    static constexpr uint8_t ENABLE2_MIC_LIGHT = 0x01;
    static constexpr uint8_t ENABLE2_LED_COLOR = 0x04;
    static constexpr uint8_t ENABLE2_LED_RESET = 0x08;
    static constexpr uint8_t ENABLE2_PLAYER_LIGHTS = 0x10;
};