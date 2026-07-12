/**
 * @file XboxController.cpp
 * @brief Implementation of Xbox controller with impulse trigger support
 * 
 * @author InputBridge Team
 * @version 3.0
 * @date 2026-07-12
 */

#include "App/Log.h"
#include "XboxController.h"
#include <SDL3/SDL_gamepad.h>

static constexpr const char* kTag = "XboxController";

// ==================== Initialization ====================

bool XboxController::IsReady() const {
    return HapticDevice::IsReady() && IsXboxController();
}

bool XboxController::IsXboxController() const {
    return IsXboxController(m_joystick);
}

bool XboxController::IsXboxController(SDL_Joystick* joystick) {
    // Vendor check first: SDL_GAMEPAD_TYPE_XBOXONE also covers third-party
    // controllers built on the same HID protocol (PDP, Mad Catz, etc.) which
    // don't have Microsoft's impulse trigger motors, so type alone isn't
    // sufficient - this mirrors SDL's own Xbox HIDAPI driver, which likewise
    // only advertises trigger-rumble support for USB_VENDOR_MICROSOFT once
    // the device has already matched SDL_GAMEPAD_TYPE_XBOXONE.
    const Uint16 vendor = SDL_GetJoystickVendor(joystick);
    if (vendor != MICROSOFT_VENDOR_ID) {
        return false;
    }

    // "Real" (not mapping-aware) type, so a user's SDL_GAMECONTROLLERCONFIG
    // remap can't cause a non-Xbox device to be treated as one here. Backed
    // by the same device table SDL's Xbox HIDAPI driver uses to decide
    // trigger-rumble support, so this can't drift out of sync the way a
    // hand-maintained product-ID list can.
    const SDL_JoystickID id = SDL_GetJoystickID(joystick);
    return SDL_GetRealGamepadTypeForID(id) == SDL_GAMEPAD_TYPE_XBOXONE;
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
        case XBOX_ONE_2015FW_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOne;
        case XBOX_ONE_S_PRODUCT_ID:
        case XBOX_ONE_S_BT_REV1_PRODUCT_ID:
        case XBOX_ONE_S_BT_REV2_PRODUCT_ID:
        case XBOX_ONE_S_BLE_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOneS;
        case XBOX_ONE_ELITE_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOneElite;
        case XBOX_ONE_ELITE2_PRODUCT_ID:
        case XBOX_ONE_ELITE2_BT_PRODUCT_ID:
        case XBOX_ONE_ELITE2_BLE_PRODUCT_ID:
            return Xbox::ControllerModel::XboxOneElite2;
        case XBOX_SERIES_X_PRODUCT_ID:
        case XBOX_SERIES_X_WIRED_PRODUCT_ID:
            return Xbox::ControllerModel::XboxSeriesX;
        default:
            // Product ID isn't in our (cosmetic-only) naming table, but
            // IsXboxController() already confirmed via SDL that this really
            // is an Xbox-family controller with trigger rumble - we just
            // don't have a specific model name for it. Report it as the
            // base model rather than Unknown, which would be misleading
            // (Unknown elsewhere means "not an Xbox controller at all").
            return Xbox::ControllerModel::XboxOne;
    }
}

// ==================== Impulse Triggers ====================

int XboxController::SetImpulseTriggers(uint8_t leftIntensity, 
                                       uint8_t rightIntensity,
                                       uint16_t durationMs) {
    if (!IsXboxController()) {
        LOG_WARN(kTag, "SetImpulseTriggers: Not an Xbox controller");
        return -1;
    }

    // Delegates to SDL's own Xbox HIDAPI driver rather than hand-rolling the
    // HID output report ourselves. SDL_RumbleJoystickTriggers() takes 0-0xFFFF
    // per motor; scale our 0-255 public range up by 257 (0xFF*257 == 0xFFFF)
    // so both ends of the range map exactly. SDL itself only has ~100 real
    // steps of resolution internally (see SDL_hidapi_xboxone.c), so this
    // scaling doesn't lose anything meaningful versus the input precision.
    //
    // This also picks up transport-specific report handling SDL's driver
    // already does for us (it builds a different-shaped report for
    // Bluetooth vs. USB) rather than the single hardcoded report layout the
    // previous hand-rolled implementation sent unconditionally.
    //
    // Sent synchronously on the calling thread, matching
    // GamepadHaptics::SendSteamControllerHaptic()'s SDL_SendJoystickEffect
    // precedent - there's no need to hop onto the worker thread via
    // RunAsync() for a single driver call like this. This also lets the
    // actual send result (rather than an unconditional success) propagate
    // back to the caller: PlayXboxTrigger() only marks the trigger active
    // when this returns 0, so a failed send - e.g. because the controller
    // was opened via the XInput backend instead of SDL's HIDAPI Xbox driver,
    // see Application::SetSDLHints() - now shows up as a failure instead of
    // silently doing nothing.
    const Uint16 leftRumble = static_cast<Uint16>(leftIntensity) * 257;
    const Uint16 rightRumble = static_cast<Uint16>(rightIntensity) * 257;

    if (!SDL_RumbleJoystickTriggers(m_joystick, leftRumble, rightRumble, durationMs)) {
        // SDL_Unsupported() (has_trigger_rumble false) and a genuine send
        // failure both surface here as a false return with SDL_GetError()
        // set log at WARN rather than ERROR since the far more common
        // cause in practice is a controller/driver combination that simply
        // doesn't support trigger rumble (e.g. XInput backend instead of
        // HIDAPI), not an actual hardware fault.
        LOG_WARN(kTag, "SetImpulseTriggers: Failed to send - %s", SDL_GetError());
        return -1;
    }

    return 0;
}

void XboxController::StopImpulseTriggers() {
    SetImpulseTriggers(0, 0, 0);
}