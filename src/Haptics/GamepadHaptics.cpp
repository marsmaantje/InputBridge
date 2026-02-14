/**
 * @file GamepadHaptics.cpp (Steam Controller Fix)
 * @brief Fixed Steam Controller support - uses only standard rumble
 * 
 * @author InputBridge Team
 * @version 3.3
 * @date 2026-02-14
 */

#include "GamepadHaptics.h"
#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cstring>

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
        SDL_Log("  Note: Steam Controller uses standard rumble only");
        SDL_Log("  Trackpad haptics not supported via SDL (requires Steam Input API)");
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
        // FIXED: Steam Controller uses ONLY standard rumble
        // Trackpad haptics are not supported via SDL (would need Steam Input API)
        if (IsSteamController()) {
            SDL_Log("Steam Controller: Using standard rumble motor");
        }
        
        // ALL controllers (including Steam Controller) use standard SDL rumble
        SDL_JoystickID id = SDL_GetJoystickID(m_joystick);
        SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
        
        if (gamepad) {
            const Uint16 lowFreq = static_cast<Uint16>(largeMagnitude * 0xFFFF);
            const Uint16 highFreq = static_cast<Uint16>(smallMagnitude * 0xFFFF);
            
            if (!SDL_RumbleGamepad(gamepad, lowFreq, highFreq, durationMs)) {
                SDL_Log("GamepadHaptics::Rumble - SDL_RumbleGamepad failed: %s",
                       SDL_GetError());
            } else {
                SDL_Log("GamepadHaptics::Rumble - Success (low=%d, high=%d, duration=%dms)",
                       lowFreq, highFreq, durationMs);
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
        SDL_Log("GamepadHaptics::SendDualSenseTrigger - Not a DualSense controller");
        return -1;  // FIXED: Return error, not silent success
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
        SDL_Log("GamepadHaptics::SendXboxImpulseTrigger - Not an Xbox controller");
        return -1;  // FIXED: Return error, not silent success
    }
    
    return m_xbox->SetImpulseTriggers(leftIntensity, rightIntensity, durationMs);
}