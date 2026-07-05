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

static constexpr const char* kTag = "DualSense";

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
    m_outputState.Reset();
    
    // Call base class init
    auto result = HapticDevice::Init();
    
    if (result) {
        // Detect connection type
        m_connectionType = DetectConnectionType();
        m_connectionTypeDetected = true;
        
        LOG_INFO(kTag, "DualSense initialized: Connection=%s", 
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
            LOG_DEBUG(kTag, "USB detected via power state (charging/charged)");
            return DualSense::ConnectionType::USB;
        }
        
        // Battery-powered means Bluetooth
        if (state == SDL_POWERSTATE_ON_BATTERY) {
            LOG_DEBUG(kTag, "Bluetooth detected via power state (on battery)");
            return DualSense::ConnectionType::Bluetooth;
        }
    }
    
    // Method 2: Check joystick path
    const char* path = SDL_GetJoystickPath(m_joystick);
    if (path) {
        std::string pathStr(path);
        std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        
        LOG_DEBUG(kTag, "Checking path: %s", path);
        
        // Explicit USB indicators
        if (pathStr.find("usb") != std::string::npos) {
            LOG_DEBUG(kTag, "USB detected via path (contains 'usb')");
            return DualSense::ConnectionType::USB;
        }
        
        // Explicit Bluetooth indicators
        if (pathStr.find("bluetooth") != std::string::npos ||
            pathStr.find("-bt-") != std::string::npos) {
            LOG_DEBUG(kTag, "Bluetooth detected via path (contains 'bluetooth'/'bt')");
            return DualSense::ConnectionType::Bluetooth;
        }
        
        // On Linux: hidraw without Bluetooth indicators = USB
        #ifdef __linux__
        if (pathStr.find("hidraw") != std::string::npos) {
            LOG_DEBUG(kTag, "USB detected via Linux hidraw");
            return DualSense::ConnectionType::USB;
        }
        #endif
    }
    
    // Method 3: Try to guess from connection latency (experimental)
    // USB typically has lower latency, but this is unreliable
    
    // Default to Bluetooth (more common for wireless play)
    // Note: Previous code defaulted to USB, but Bluetooth is more common
    LOG_DEBUG(kTag, "Defaulting to Bluetooth (could not determine definitively)");
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
        LOG_WARN(kTag, "SetTriggerEffect - Invalid trigger: %s", trigger.c_str());
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
        LOG_WARN(kTag, "SetTriggerEffect - Failed to apply effect");
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
        LOG_WARN(kTag, "Unknown effect type '%s'", effectType.c_str());
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
        if (!SendOutput()) {
            LOG_WARN(kTag, "Failed to send output state");
        }
    });
}

bool DualSenseController::SendOutput() {
    // This buffer is ONLY the DS5EffectsState_t-equivalent payload (47 bytes),
    // starting at ucEnableBits1. SDL's PS5 HIDAPI driver prepends the report
    // ID itself (and, on Bluetooth, a sequence/tag byte plus a trailing CRC),
    // so this must NOT include a report ID/sequence byte of our own - doing so
    // shifts every field (including the trigger effect bytes) out of place and
    // silently breaks adaptive triggers. SDL also auto-detects USB vs
    // Bluetooth, so one buffer works for both connection types.
    std::array<uint8_t, EFFECTS_PAYLOAD_SIZE> data{};

    // ucEnableBits1 (offset 0): rumble + which trigger(s) we're driving.
    // The MODIFY_RIGHT/LEFT_TRIGGER bits are what actually tell the firmware
    // to apply rgucRightTriggerEffect/rgucLeftTriggerEffect - without them the
    // trigger effect bytes are silently ignored.
    uint8_t enableBits1 = ENABLE1_MODIFY_RIGHT_TRIGGER | ENABLE1_MODIFY_LEFT_TRIGGER;
    if (m_outputState.leftRumble != 0 || m_outputState.rightRumble != 0) {
        enableBits1 |= ENABLE1_RUMBLE_EMULATION | ENABLE1_DISABLE_AUDIO_HAPTICS;
    }
    data[0] = enableBits1;

    // ucEnableBits2 (offset 1): LED color / player lights.
    uint8_t enableBits2 = 0;
    const bool ledSet = (m_outputState.ledColor.red != 0 ||
                          m_outputState.ledColor.green != 0 ||
                          m_outputState.ledColor.blue != 0);
    if (ledSet) {
        enableBits2 |= ENABLE2_LED_COLOR;
    }
    if (m_outputState.playerLEDs != 0) {
        enableBits2 |= ENABLE2_PLAYER_LIGHTS;
    }
    data[1] = enableBits2;

    // ucRumbleRight / ucRumbleLeft (offsets 2-3)
    data[2] = m_outputState.rightRumble;
    data[3] = m_outputState.leftRumble;

    // ucMicLightMode (offset 8) - reuse muteLED state for the mic/mute light
    data[8] = m_outputState.muteLED;

    // rgucRightTriggerEffect / rgucLeftTriggerEffect (offsets 10 / 21)
    std::memcpy(&data[RIGHT_TRIGGER_OFFSET], m_outputState.rightTriggerEffect.data(), 11);
    std::memcpy(&data[LEFT_TRIGGER_OFFSET], m_outputState.leftTriggerEffect.data(), 11);

    // ucLedBrightness (offset 42), ucPadLights (offset 43),
    // ucLedRed/Green/Blue (offsets 44-46)
    data[LED_BRIGHTNESS_OFFSET] = m_outputState.ledBrightness;
    data[PAD_LIGHTS_OFFSET] = m_outputState.playerLEDs;
    data[LED_RED_OFFSET] = m_outputState.ledColor.red;
    data[LED_GREEN_OFFSET] = m_outputState.ledColor.green;
    data[LED_BLUE_OFFSET] = m_outputState.ledColor.blue;

    LOG_DEBUG(kTag, "Sending effects payload, size=%zu (connection=%s)", data.size(),
              GetConnectionType() == DualSense::ConnectionType::USB ? "USB" : "Bluetooth");
    LOG_DEBUG(kTag, "  EnableBits: [0]=0x%02X [1]=0x%02X", data[0], data[1]);
    LOG_DEBUG(kTag, "  Rumble: L=%d R=%d", data[3], data[2]);
    LOG_DEBUG(kTag, "  Triggers: R[0]=0x%02X L[0]=0x%02X",
           m_outputState.rightTriggerEffect[0], m_outputState.leftTriggerEffect[0]);

    if (!SDL_SendJoystickEffect(m_joystick, data.data(), data.size())) {
        LOG_WARN(kTag, "DualSense: Send failed - %s", SDL_GetError());
        return false;
    }

    LOG_DEBUG(kTag, "Effects payload sent successfully");
    return true;
}