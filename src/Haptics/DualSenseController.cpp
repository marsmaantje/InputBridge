/**
 * @file DualSenseController.cpp
 * @brief Implementation of Sony DualSense controller with adaptive triggers
 * 
 * @author InputBridge Team
 * @version 2.1
 * @date 2026-02-14
 */

#include "App/Log.h"
#include "DualSenseController.h"
#include "DualSenseTriggerEffectGenerator.h"
#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cstring>
#include <cctype>

// ==================== OutputState Implementation ====================

DualSense::OutputState::OutputState() 
    : rightRumble(0)
    , leftRumble(0)
    , ledBrightness(0)
    , playerLEDs(0)
    , muteLED(0)
{
    rightTriggerEffect.fill(0);
    leftTriggerEffect.fill(0);
    // Black LED = don't change
    ledColor = RGBColor(0, 0, 0);
}

void DualSense::OutputState::Reset() {
    rightRumble = 0;
    leftRumble = 0;
    rightTriggerEffect.fill(0);
    leftTriggerEffect.fill(0);
    ledColor = RGBColor(0, 0, 0);
    ledBrightness = 0;
    playerLEDs = 0;
    muteLED = 0;
}

// ==================== Initialization ====================

InputBridge::Result<bool, InputBridge::HapticError> DualSenseController::Init() {
    m_connectionTypeDetected = false;
    m_bluetoothSequence = 0;
    m_outputState.Reset();
    
    // Call base class init
    auto result = HapticDevice::Init();
    
    if (result) {
        // Detect connection type
        m_connectionType = DetectConnectionType();
        m_connectionTypeDetected = true;
        
        LOG_INFO("DualSense", "DualSense initialized: Connection=%s", 
               m_connectionType == DualSense::ConnectionType::USB ? "USB" : "Bluetooth");
    }
    
    return result;
}

bool DualSenseController::IsReady() const {
    return HapticDevice::IsReady() && IsDualSense();
}

bool DualSenseController::IsDualSense() const {
    const Uint16 vendor = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);
    
    return (vendor == SONY_VENDOR_ID && 
            (product == DUALSENSE_PRODUCT_ID || product == DUALSENSE_EDGE_PRODUCT_ID));
}

// ==================== Connection Detection ====================

DualSense::ConnectionType DualSenseController::DetectConnectionType() const {
    if (!m_joystick) {
        return DualSense::ConnectionType::Unknown;
    }

    SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
    
    // Method 1: Check power state (most reliable)
    if (gamepad) {
        int percent = 0;
        SDL_PowerState state = SDL_GetGamepadPowerInfo(gamepad, &percent);
        
        // USB devices report as charging or charged
        if (state == SDL_POWERSTATE_CHARGING || state == SDL_POWERSTATE_CHARGED) {
            LOG_DEBUG("DualSense", "USB detected via power state (charging/charged)");
            return DualSense::ConnectionType::USB;
        }
        
        // Battery-powered means Bluetooth
        if (state == SDL_POWERSTATE_ON_BATTERY) {
            LOG_DEBUG("DualSense", "Bluetooth detected via power state (on battery)");
            return DualSense::ConnectionType::Bluetooth;
        }
    }
    
    // Method 2: Check joystick path
    const char* path = SDL_GetJoystickPath(m_joystick);
    if (path) {
        std::string pathStr(path);
        std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        
        LOG_DEBUG("DualSense", "Checking path: %s", path);
        
        // Explicit USB indicators
        if (pathStr.find("usb") != std::string::npos) {
            LOG_DEBUG("DualSense", "USB detected via path (contains 'usb')");
            return DualSense::ConnectionType::USB;
        }
        
        // Explicit Bluetooth indicators
        if (pathStr.find("bluetooth") != std::string::npos ||
            pathStr.find("-bt-") != std::string::npos) {
            LOG_DEBUG("DualSense", "Bluetooth detected via path (contains 'bluetooth'/'bt')");
            return DualSense::ConnectionType::Bluetooth;
        }
        
        // On Linux: hidraw without Bluetooth indicators = USB
        #ifdef __linux__
        if (pathStr.find("hidraw") != std::string::npos) {
            LOG_DEBUG("DualSense", "USB detected via Linux hidraw");
            return DualSense::ConnectionType::USB;
        }
        #endif
    }
    
    // Method 3: Try to guess from connection latency (experimental)
    // USB typically has lower latency, but this is unreliable
    
    // Default to Bluetooth (more common for wireless play)
    // Note: Previous code defaulted to USB, but Bluetooth is more common
    LOG_DEBUG("DualSense", "Defaulting to Bluetooth (could not determine definitively)");
    return DualSense::ConnectionType::Bluetooth;
}

DualSense::ConnectionType DualSenseController::GetConnectionType() const {
    // Detect on first call (cached)
    if (!m_connectionTypeDetected) {
        // Cast away const to cache the result
        const_cast<DualSenseController*>(this)->m_connectionType = DetectConnectionType();
        const_cast<DualSenseController*>(this)->m_connectionTypeDetected = true;
    }
    return m_connectionType;
}

// ==================== Trigger Effects ====================

int DualSenseController::SetTriggerEffect(const std::string& trigger,
                                         const std::string& effectType,
                                         const std::map<std::string, int>& params) {
    const bool updateLeft = (trigger == "left" || trigger == "both");
    const bool updateRight = (trigger == "right" || trigger == "both");
    
    if (!updateLeft && !updateRight) {
        LOG_WARN("DualSense", "SetTriggerEffect - Invalid trigger: %s", trigger.c_str());
        return -1;
    }
    
    // Apply effect to appropriate trigger(s)
    bool success = true;
    if (updateLeft) {
        success &= ApplyTriggerEffect(m_outputState.leftTriggerEffect.data(), effectType, params);
    }
    if (updateRight) {
        success &= ApplyTriggerEffect(m_outputState.rightTriggerEffect.data(), effectType, params);
    }
    
    if (!success) {
        LOG_WARN("DualSense", "SetTriggerEffect - Failed to apply effect");
        return -1;
    }
    
    // Immediately apply the state
    ApplyOutputState();
    
    return 0;
}

bool DualSenseController::ApplyTriggerEffect(uint8_t* triggerData,
                                            const std::string& effectType,
                                            const std::map<std::string, int>& params) {
    // Helper to get clamped parameter
    auto getParam = [&params](const std::string& key, int defaultVal, int minVal, int maxVal) -> uint8_t {
        int value = params.count(key) ? params.at(key) : defaultVal;
        return static_cast<uint8_t>(std::clamp(value, minVal, maxVal));
    };
    
    // Apply the effect using DualSenseTriggerEffectGenerator
    if (effectType == "off") {
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Off(triggerData, 0);
    }
    else if (effectType == "feedback") {
        uint8_t position = getParam("position", 0, 0, 9);
        uint8_t strength = getParam("strength", 5, 0, 8);
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Feedback(triggerData, 0, position, strength);
    }
    else if (effectType == "weapon") {
        uint8_t startPos = getParam("start_position", 2, 2, 7);
        uint8_t endPos = getParam("end_position", 7, 0, 8);
        uint8_t strength = getParam("strength", 5, 0, 8);
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Weapon(triggerData, 0, startPos, endPos, strength);
    }
    else if (effectType == "vibration") {
        uint8_t position = getParam("position", 0, 0, 9);
        uint8_t amplitude = getParam("amplitude", 5, 0, 8);
        uint8_t frequency = getParam("frequency", 10, 0, 255);
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Vibration(triggerData, 0, position, amplitude, frequency);
    }
    else if (effectType == "bow") {
        uint8_t startPos = getParam("start_position", 0, 0, 8);
        uint8_t endPos = getParam("end_position", 8, 0, 8);
        uint8_t strength = getParam("strength", 5, 0, 8);
        uint8_t snapForce = getParam("snap_force", 5, 0, 8);
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Bow(triggerData, 0, startPos, endPos, strength, snapForce);
    }
    else if (effectType == "galloping") {
        uint8_t startPos = getParam("start_position", 0, 0, 9);
        uint8_t endPos = getParam("end_position", 9, 0, 9);
        uint8_t firstFoot = getParam("first_foot", 2, 0, 6);
        uint8_t secondFoot = getParam("second_foot", 7, 0, 7);
        uint8_t frequency = getParam("frequency", 10, 0, 255);
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Galloping(triggerData, 0, startPos, endPos,
                                                          firstFoot, secondFoot, frequency);
    }
    else if (effectType == "machine") {
        uint8_t startPos = getParam("start_position", 0, 0, 9);
        uint8_t endPos = getParam("end_position", 9, 0, 9);
        uint8_t amplitudeA = getParam("amplitude_a", 4, 0, 7);
        uint8_t amplitudeB = getParam("amplitude_b", 4, 0, 7);
        uint8_t frequency = getParam("frequency", 10, 0, 255);
        uint8_t period = getParam("period", 0, 0, 2);
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Machine(triggerData, 0, startPos, endPos,
                                                        amplitudeA, amplitudeB, frequency, period);
    }
    else {
        LOG_WARN("DualSense", "Unknown effect type '%s'", effectType.c_str());
        return ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Off(triggerData, 0);
    }
}

void DualSenseController::DisableTriggerEffects() {
    ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Off(m_outputState.leftTriggerEffect.data(), 0);
    ExtendInput::DataTools::DualSense::DualSenseTriggerEffectGenerator::Off(m_outputState.rightTriggerEffect.data(), 0);
    ApplyOutputState();
}

// ==================== LED Control ====================

void DualSenseController::SetLEDColor(const DualSense::RGBColor& color) {
    m_outputState.ledColor = color;
}

void DualSenseController::SetLEDBrightness(uint8_t brightness) {
    m_outputState.ledBrightness = brightness;
}

void DualSenseController::SetPlayerLEDs(uint8_t playerMask) {
    m_outputState.playerLEDs = playerMask & 0x1F;  // 5-bit mask
}

void DualSenseController::SetMuteLED(uint8_t state) {
    m_outputState.muteLED = state;
}

// ==================== Rumble Motors ====================

void DualSenseController::SetRumble(uint8_t leftIntensity, uint8_t rightIntensity) {
    m_outputState.leftRumble = leftIntensity;
    m_outputState.rightRumble = rightIntensity;
}

// ==================== Protocol Implementation ====================

void DualSenseController::ApplyOutputState() {
    RunAsync([this]() {
        const DualSense::ConnectionType connType = GetConnectionType();
        
        bool success = false;
        if (connType == DualSense::ConnectionType::USB) {
            success = SendUSBOutput();
        } else {
            success = SendBluetoothOutput();
        }
        
        if (!success) {
            LOG_WARN("DualSense", "Failed to send output state");
        }
    });
}

bool DualSenseController::SendUSBOutput() {
    // USB report is 63 bytes (bluetooth 48)
    std::array<uint8_t, USB_REPORT_SIZE> data{};
    
    data[0] = USB_REPORT_ID;  // 0x02
    
    // Feature flags byte 1 (offset 1)
    // Enable HID mode and rumble
    data[1] = USB_FLAG_ENABLE_HID | USB_FLAG_ENABLE_RUMBLE;
    
    // Feature flags byte 2 (offset 2)
    // Only enable LED/player flags if they're actually being set
    uint8_t flags2 = FLAG2_ENABLE_HAPTICS;  // Always enable haptics for triggers
    
    // Only enable LED if not black
    if (m_outputState.ledColor.red != 0 || 
        m_outputState.ledColor.green != 0 || 
        m_outputState.ledColor.blue != 0) {
        flags2 |= FLAG2_ENABLE_LED_COLOR;
    }
    
    // Only enable player LEDs if set
    if (m_outputState.playerLEDs != 0) {
        flags2 |= FLAG2_ENABLE_PLAYER_LEDS;
    }
    
    data[2] = flags2;
    
    // Rumble motors (offset 3-4)
    data[3] = m_outputState.rightRumble;
    data[4] = m_outputState.leftRumble;
    
    // Mute LED (offset 9)
    data[9] = m_outputState.muteLED;
    
    // Right trigger at offset 11
    std::memcpy(&data[USB_RIGHT_TRIGGER_OFFSET], m_outputState.rightTriggerEffect.data(), 11);
    
    // Left trigger at offset 22
    std::memcpy(&data[USB_LEFT_TRIGGER_OFFSET], m_outputState.leftTriggerEffect.data(), 11);
    
    // LED color (offset 39-41)
    data[39] = m_outputState.ledColor.red;
    data[40] = m_outputState.ledColor.green;
    data[41] = m_outputState.ledColor.blue;
    
    // Player LEDs (offset 42)
    data[42] = m_outputState.playerLEDs;
    
    // LED brightness (offset 43)
    data[43] = m_outputState.ledBrightness;
    
    LOG_DEBUG("DualSense", "USB: Sending report, size=%zu", data.size());
    LOG_DEBUG("DualSense", "  Flags: [1]=0x%02X [2]=0x%02X", data[1], data[2]);
    LOG_DEBUG("DualSense", "  Rumble: L=%d R=%d", data[4], data[3]);
    LOG_DEBUG("DualSense", "  Triggers: R[0]=0x%02X L[0]=0x%02X", 
           m_outputState.rightTriggerEffect[0], m_outputState.leftTriggerEffect[0]);
    
    if (!SDL_SendJoystickEffect(m_joystick, data.data(), data.size())) {
        LOG_WARN("DualSense", "DualSense USB: Send failed - %s", SDL_GetError());
        return false;
    }
    
    LOG_DEBUG("DualSense", "USB: Report sent successfully");
    return true;
}

bool DualSenseController::SendBluetoothOutput() {
    // Bluetooth report is 78 bytes
    std::array<uint8_t, BT_REPORT_SIZE> data{};
    
    data[0] = BT_REPORT_ID;  // 0x31
    
    // Sequence byte (offset 1)
    // Increment sequence counter (0-15) and set enable flag
    m_bluetoothSequence = (m_bluetoothSequence + 1) & 0x0F;
    data[1] = 0x10 | m_bluetoothSequence;  // 0x10 = enable flag
    
    // Feature flags byte 1 (offset 2)
    data[2] = BT_FLAG_ENABLE_RUMBLE_EMULATION;
    
    // Feature flags byte 2 (offset 3)
    // Only enable LED/player flags if they're actually being set
    uint8_t flags2 = FLAG2_ENABLE_HAPTICS;  // Always enable haptics for triggers
    
    // Only enable LED if not black
    if (m_outputState.ledColor.red != 0 || 
        m_outputState.ledColor.green != 0 || 
        m_outputState.ledColor.blue != 0) {
        flags2 |= FLAG2_ENABLE_LED_COLOR;
    }
    
    // Only enable player LEDs if set
    if (m_outputState.playerLEDs != 0) {
        flags2 |= FLAG2_ENABLE_PLAYER_LEDS;
    }
    
    data[3] = flags2;
    
    // Rumble motors (offset 4-5)
    data[4] = m_outputState.rightRumble;
    data[5] = m_outputState.leftRumble;
    
    // Mute LED (offset 9)
    data[9] = m_outputState.muteLED;
    
    // Bluetooth uses different offsets!
    // Right trigger at offset 22 (not 23!)
    std::memcpy(&data[BT_RIGHT_TRIGGER_OFFSET], m_outputState.rightTriggerEffect.data(), 11);
    
    // Left trigger at offset 33 (not 34!)
    std::memcpy(&data[BT_LEFT_TRIGGER_OFFSET], m_outputState.leftTriggerEffect.data(), 11);
    
    // LED color (offset 45-47)
    data[45] = m_outputState.ledColor.red;
    data[46] = m_outputState.ledColor.green;
    data[47] = m_outputState.ledColor.blue;
    
    // Player LEDs (offset 44)
    data[44] = m_outputState.playerLEDs;
    
    LOG_DEBUG("DualSense", "BT: Sending report, size=%zu, seq=%d", data.size(), m_bluetoothSequence);
    LOG_DEBUG("DualSense", "  Flags: [1]=0x%02X [2]=0x%02X [3]=0x%02X", data[1], data[2], data[3]);
    LOG_DEBUG("DualSense", "  Rumble: L=%d R=%d", data[5], data[4]);
    LOG_DEBUG("DualSense", "  Triggers: R[0]=0x%02X L[0]=0x%02X", 
           m_outputState.rightTriggerEffect[0], m_outputState.leftTriggerEffect[0]);
    
    // SDL handles CRC32 for Bluetooth automatically
    if (!SDL_SendJoystickEffect(m_joystick, data.data(), data.size())) {
        LOG_WARN("DualSense", "DualSense BT: Send failed - %s", SDL_GetError());
        return false;
    }
    
    LOG_DEBUG("DualSense", "BT: Report sent successfully");
    return true;
}