/**
 * @file XboxController.cpp
 * @brief Implementation of Xbox controller with impulse trigger support
 * 
 * @author InputBridge Team
 * @version 2.0
 * @date 2026-02-14
 */

#include "App/Log.h"
#include "XboxController.h"
#include <SDL3/SDL_gamepad.h>
#include <array>

// ==================== Initialization ====================

bool XboxController::IsReady() const {
    return HapticDevice::IsReady() && IsXboxController();
}

bool XboxController::IsXboxController() const {
    const Uint16 vendor = SDL_GetJoystickVendor(m_joystick);
    
    if (vendor != MICROSOFT_VENDOR_ID) {
        return false;
    }
    
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);
    
    // Check if this is a known Xbox controller with impulse triggers
    return (product == XBOX_ONE_PRODUCT_ID ||
            product == XBOX_ONE_S_PRODUCT_ID ||
            product == XBOX_ONE_ELITE_PRODUCT_ID ||
            product == XBOX_ONE_ELITE2_PRODUCT_ID ||
            product == XBOX_SERIES_X_PRODUCT_ID);
}

// ==================== Model Detection ====================

Xbox::ControllerModel XboxController::GetModel() const {
    return DetectModel();
}

Xbox::ControllerModel XboxController::DetectModel() const {
    if (!IsXboxController()) {
        return Xbox::ControllerModel::Unknown;
    }
    
    const Uint16 product = SDL_GetJoystickProduct(m_joystick);
    
    switch (product) {
        case XBOX_ONE_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOne;
        case XBOX_ONE_S_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOneS;
        case XBOX_ONE_ELITE_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOneElite;
        case XBOX_ONE_ELITE2_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOneElite2;
        case XBOX_SERIES_X_PRODUCT_ID:
            return Xbox::ControllerModel::XboxSeriesX;
        default:
            return Xbox::ControllerModel::Unknown;
    }
}

// ==================== Impulse Triggers ====================

int XboxController::SetImpulseTriggers(uint8_t leftIntensity, 
                                       uint8_t rightIntensity,
                                       uint16_t durationMs) {
    if (!IsXboxController()) {
        LOG_WARN("XboxController", "SetImpulseTriggers: Not an Xbox controller");
        return -1;
    }
    
    RunAsync([this, leftIntensity, rightIntensity, durationMs]() {
        Xbox::ImpulseTriggerState state;
        state.leftTriggerMotor = leftIntensity;
        state.rightTriggerMotor = rightIntensity;
        state.durationMs = durationMs;
        
        if (!SendImpulseTriggerCommand(state)) {
            LOG_ERROR("XboxController", "SetImpulseTriggers: Failed to send command");
        }
    });
    
    return 0;
}

void XboxController::StopImpulseTriggers() {
    SetImpulseTriggers(0, 0, 0);
}

// ==================== Protocol Implementation ====================

bool XboxController::SendImpulseTriggerCommand(const Xbox::ImpulseTriggerState& state) {
    /**
     * Xbox Impulse Trigger Protocol
     * 
     * Report structure:
     * [0] = Report ID (0x03)
     * [1] = Enable flags (0xFF = enable all motors)
     * [2] = Left trigger motor magnitude (0-255)
     * [3] = Right trigger motor magnitude (0-255)
     * [4] = Left handle motor magnitude (0-255)
     * [5] = Right handle motor magnitude (0-255)
     * [6] = Duration low byte
     * [7] = Duration high byte
     */
    
    std::array<uint8_t, IMPULSE_TRIGGER_REPORT_SIZE> data{};
    
    data[0] = IMPULSE_TRIGGER_REPORT_ID;  // 0x03
    data[1] = 0xFF;  // Enable all motors
    data[2] = state.leftTriggerMotor;
    data[3] = state.rightTriggerMotor;
    data[4] = 0;     // Left handle motor (not used for triggers)
    data[5] = 0;     // Right handle motor (not used for triggers)
    
    // Duration in milliseconds (little-endian)
    data[6] = static_cast<uint8_t>(state.durationMs & 0xFF);
    data[7] = static_cast<uint8_t>((state.durationMs >> 8) & 0xFF);
    
    LOG_DEBUG("XboxController", "Impulse: Report=0x%02X, L=%d, R=%d, Duration=%dms",
           data[0], state.leftTriggerMotor, state.rightTriggerMotor, state.durationMs);
    
    if (!SDL_SendJoystickEffect(m_joystick, data.data(), data.size())) {
        LOG_ERROR("XboxController", "Impulse: Send failed - %s", SDL_GetError());
        return false;
    }
    
    LOG_DEBUG("XboxController", "Impulse: Report sent successfully");
    return true;
}