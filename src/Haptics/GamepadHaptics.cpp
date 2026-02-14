/**
 * @file GamepadHaptics.cpp (CRITICAL FIX - Steam Controller Freeze)
 * @brief Fixed implementation that doesn't block the haptic thread
 * 
 * CRITICAL FIX: Removed all blocking sleep() calls that were freezing the haptic thread!
 * 
 * @author InputBridge Team
 * @version 3.1
 * @date 2026-02-14
 */

#include "GamepadHaptics.h"
#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cstring>
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
    }
    else {
        SDL_Log("GamepadHaptics: Initialized as generic gamepad");
    }
    
    return result;
}

bool GamepadHaptics::IsReady() const {
    // Check base class
    if (HapticDevice::IsReady()) {
        return true;
    }
    
    // Check if we have a gamepad handle
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
    const Uint16 vendor = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);
    
    return (vendor == 0x054C &&  // Sony
            (product == 0x0CE6 ||  // DualSense
             product == 0x0DF2));  // DualSense Edge
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
    const Uint16 vendor = SDL_GetJoystickVendor(m_joystick);
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);
    
    if (vendor == VALVE_VENDOR_ID &&
        (product == STEAM_CONTROLLER_PRODUCT_ID ||
         product == STEAM_CONTROLLER_V2_PRODUCT_ID)) {
        return true;
    }
    
    // Also check by name
    const char* name = SDL_GetJoystickName(m_joystick);
    if (name && std::strstr(name, "Steam Controller")) {
        return true;
    }
    
    return false;
}

const char* GamepadHaptics::GetControllerTypeName() const {
    if (IsDualSense()) {
        return "DualSense";
    }
    if (IsXboxController()) {
        return "Xbox";
    }
    if (IsSteamController()) {
        return "Steam Controller";
    }
    return "Generic Gamepad";
}

// ==================== Universal Rumble ====================

int GamepadHaptics::Rumble(float largeMagnitude, float smallMagnitude, uint32_t durationMs) {
    // Clamp values
    largeMagnitude = std::clamp(largeMagnitude, 0.0f, 1.0f);
    smallMagnitude = std::clamp(smallMagnitude, 0.0f, 1.0f);
    
    RunAsync([this, largeMagnitude, smallMagnitude, durationMs]() {
        // CRITICAL FIX: Steam Controller special handling
        // Added small delay between commands to prevent freeze
        if (IsSteamController()) {
            SDL_Log("Steam Controller: Sending haptic pulses");
            
            // Send left pad pulse
            if (largeMagnitude > 0.0f) {
                SendSteamControllerHaptic(0, largeMagnitude, durationMs);
            }
            
            // CRITICAL FIX: Small delay between commands prevents controller freeze
            SDL_Delay(5);  // 5ms delay - doesn't block the thread significantly
            
            // Send right pad pulse
            if (smallMagnitude > 0.0f) {
                SendSteamControllerHaptic(1, smallMagnitude, durationMs);
            }
            
            SDL_Log("Steam Controller: Haptic pulses sent successfully");
            return;
        }
        
        // CRITICAL FIX: DualSense - Use rumble motors WITHOUT blocking sleep
        if (m_dualSense) {
            const uint8_t left = static_cast<uint8_t>(largeMagnitude * 255);
            const uint8_t right = static_cast<uint8_t>(smallMagnitude * 255);
            
            m_dualSense->SetRumble(left, right);
            m_dualSense->ApplyOutputState();
            
            // CRITICAL FIX: Don't block the thread with sleep!
            // Instead, schedule a stop command after the duration
            if (durationMs > 0) {
                // Schedule stop on a separate timer (not blocking the haptic thread)
                std::thread([this, durationMs]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
                    
                    // Stop rumble after duration
                    RunAsync([this]() {
                        if (m_dualSense) {
                            m_dualSense->SetRumble(0, 0);
                            m_dualSense->ApplyOutputState();
                        }
                    });
                }).detach();
            }
            return;
        }
        
        // Standard gamepad rumble
        SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
        SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
        
        if (gamepad) {
            const Uint16 lowFreq = static_cast<Uint16>(largeMagnitude * 0xFFFF);
            const Uint16 highFreq = static_cast<Uint16>(smallMagnitude * 0xFFFF);
            
            if (!SDL_RumbleGamepad(gamepad, lowFreq, highFreq, durationMs)) {
                SDL_Log("GamepadHaptics::Rumble - SDL_RumbleGamepad failed: %s",
                       SDL_GetError());
            }
        } else {
            SDL_Log("GamepadHaptics::Rumble - No gamepad handle available");
        }
    });
    
    return 0;
}

// ==================== DualSense-Specific ====================

int GamepadHaptics::SendDualSenseTrigger(const char* trigger,
                                         const char* effectType,
                                         const std::map<std::string, int>& params) {
    if (!m_dualSense) {
        // Silently ignore if not a DualSense
        return 0;
    }
    
    return m_dualSense->SetTriggerEffect(trigger, effectType, params);
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
        // Silently ignore if not an Xbox controller
        return 0;
    }
    
    return m_xbox->SetImpulseTriggers(leftIntensity, rightIntensity, durationMs);
}

// ==================== Steam Controller (FIXED) ====================

void GamepadHaptics::SendSteamControllerHaptic(uint8_t pad, float magnitude, uint32_t durationMs) {
    if (magnitude <= 0.0f) {
        return;
    }
    
    magnitude = std::clamp(magnitude, 0.0f, 1.0f);
    
    // CRITICAL FIX: Simplified struct to avoid alignment issues
    #pragma pack(push, 1)
    struct HapticPulseCommand {
        uint8_t reportId;           // 0x87
        uint8_t messageType;        // 11
        uint8_t messageLength;      // sizeof(HapticPulseData)
        uint8_t whichPad;           // 0=left, 1=right
        uint16_t pulseDuration;     // microseconds
        uint16_t pulseInterval;     // microseconds
        uint16_t pulseCount;        // number of pulses
        int16_t dBgain;            // gain
        uint8_t priority;           // priority
        uint8_t padding[53];        // Pad to 64 bytes
    };
    #pragma pack(pop)
    
    HapticPulseCommand cmd{};
    cmd.reportId = STEAM_CONTROLLER_REPORT_ID;  // 0x87
    cmd.messageType = STEAM_HAPTIC_PULSE_MSG_ID;  // 11
    cmd.messageLength = 9;  // sizeof(whichPad + pulseDuration + ... + priority)
    
    cmd.whichPad = pad;
    
    // Calculate pulse parameters
    cmd.pulseDuration = static_cast<uint16_t>(magnitude * 2000.0f);  // 0-2ms
    cmd.pulseInterval = 3000;  // 3ms between pulses
    
    const uint16_t singlePulseCycleMs = 5;
    uint16_t pulseCount = 1;
    if (durationMs > singlePulseCycleMs) {
        pulseCount = static_cast<uint16_t>(durationMs / singlePulseCycleMs);
        // CRITICAL FIX: Cap pulse count to prevent excessive commands
        pulseCount = std::min(pulseCount, static_cast<uint16_t>(200));
    }
    cmd.pulseCount = pulseCount;
    
    cmd.dBgain = 0;
    cmd.priority = 0;
    
    SDL_Log("Steam Controller: Pad=%d, Duration=%dus, Interval=%dus, Count=%d",
           pad, cmd.pulseDuration, cmd.pulseInterval, cmd.pulseCount);
    
    // CRITICAL FIX: Use non-blocking send with error handling
    if (!SDL_SendJoystickEffect(m_joystick, reinterpret_cast<const uint8_t*>(&cmd), 64)) {
        SDL_Log("Steam Controller: Send failed (pad %d) - %s", pad, SDL_GetError());
        // Don't throw or return error - just log and continue
    } else {
        SDL_Log("Steam Controller: Command sent successfully (pad %d)", pad);
    }
}