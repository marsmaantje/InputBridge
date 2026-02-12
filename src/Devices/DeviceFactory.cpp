#include "DeviceFactory.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/SteeringWheelHaptics.h"
#include <SDL3/SDL_log.h>

namespace InputBridge {

std::optional<DeviceCreationResult> DeviceFactory::CreateDevice(SDL_JoystickID instance_id) {
    SDL_JoystickType type = SDL_GetJoystickTypeForID(instance_id);
    
    switch (type) {
        case SDL_JOYSTICK_TYPE_WHEEL:
            return CreateWheelDevice(instance_id);
            
        case SDL_JOYSTICK_TYPE_GAMEPAD:
            return CreateGamepadDevice(instance_id);
            
        case SDL_JOYSTICK_TYPE_FLIGHT_STICK:
        case SDL_JOYSTICK_TYPE_THROTTLE:
        case SDL_JOYSTICK_TYPE_ARCADE_STICK:
        case SDL_JOYSTICK_TYPE_UNKNOWN:
            return CreateGenericDevice(instance_id);
            
        default:
            SDL_Log("Unsupported device type: %d", static_cast<int>(type));
            return std::nullopt;
    }
}

std::optional<DeviceCreationResult> DeviceFactory::CreateWheelDevice(SDL_JoystickID instance_id) {
    SDL_Joystick* joystick = SDL_OpenJoystick(instance_id);
    if (!joystick) {
        SDL_Log("Failed to open steering wheel device %d: %s", instance_id, SDL_GetError());
        return std::nullopt;
    }
    
    DeviceState state = CreateBaseDeviceState(joystick, instance_id);
    state.is_gamepad = false;
    state.gamepad = nullptr;
    state.joystick = joystick;
    
    auto haptic = CreateHapticDevice(joystick, SDL_JOYSTICK_TYPE_WHEEL);
    
    return DeviceCreationResult{std::move(state), std::move(haptic)};
}

std::optional<DeviceCreationResult> DeviceFactory::CreateGamepadDevice(SDL_JoystickID instance_id) {
    SDL_Gamepad* gamepad = SDL_OpenGamepad(instance_id);
    if (!gamepad) {
        SDL_Log("Failed to open gamepad device %d: %s", instance_id, SDL_GetError());
        return std::nullopt;
    }
    
    SDL_Joystick* joystick = SDL_GetGamepadJoystick(gamepad);
    if (!joystick) {
        SDL_Log("Failed to get joystick from gamepad %d", instance_id);
        SDL_CloseGamepad(gamepad);
        return std::nullopt;
    }
    
    DeviceState state = CreateBaseDeviceState(joystick, instance_id);
    state.is_gamepad = true;
    state.gamepad = gamepad;
    state.joystick = joystick;
    state.name = SDL_GetGamepadName(gamepad); // Override with gamepad name
    
    auto haptic = CreateHapticDevice(joystick, SDL_JOYSTICK_TYPE_GAMEPAD);
    
    return DeviceCreationResult{std::move(state), std::move(haptic)};
}

std::optional<DeviceCreationResult> DeviceFactory::CreateGenericDevice(SDL_JoystickID instance_id) {
    SDL_Joystick* joystick = SDL_OpenJoystick(instance_id);
    if (!joystick) {
        SDL_Log("Failed to open generic device %d: %s", instance_id, SDL_GetError());
        return std::nullopt;
    }
    
    DeviceState state = CreateBaseDeviceState(joystick, instance_id);
    state.is_gamepad = false;
    state.gamepad = nullptr;
    state.joystick = joystick;
    
    // TODO: Implement haptics for flight sticks when FlightStickHaptics class exists
    auto haptic = CreateHapticDevice(joystick, SDL_GetJoystickTypeForID(instance_id));
    
    return DeviceCreationResult{std::move(state), std::move(haptic)};
}

DeviceState DeviceFactory::CreateBaseDeviceState(SDL_Joystick* joystick, 
                                                  SDL_JoystickID instance_id) {
    DeviceState state{};
    state.instance_id = instance_id;
    state.name = SDL_GetJoystickName(joystick);
    state.num_axes = SDL_GetNumJoystickAxes(joystick);
    state.num_buttons = SDL_GetNumJoystickButtons(joystick);
    state.num_hats = SDL_GetNumJoystickHats(joystick);
    
    return state;
}

std::unique_ptr<HapticDevice> DeviceFactory::CreateHapticDevice(SDL_Joystick* joystick, 
                                                                 SDL_JoystickType device_type) {
    if (!SDL_IsJoystickHaptic(joystick)) {
        return nullptr;
    }
    
    std::unique_ptr<HapticDevice> haptic;
    
    switch (device_type) {
        case SDL_JOYSTICK_TYPE_GAMEPAD:
            haptic = std::make_unique<GamepadHaptics>(joystick);
            break;
            
        case SDL_JOYSTICK_TYPE_WHEEL:
            haptic = std::make_unique<SteeringWheelHaptics>(joystick);
            break;
            
        case SDL_JOYSTICK_TYPE_FLIGHT_STICK:
        case SDL_JOYSTICK_TYPE_THROTTLE:
            // TODO: Create FlightStickHaptics when implemented
            SDL_Log("Haptics for flight stick/throttle not yet implemented");
            return nullptr;
            
        default:
            SDL_Log("Unknown haptic device type: %d", static_cast<int>(device_type));
            return nullptr;
    }
    
    if (haptic && !haptic->Init()) {
        SDL_Log("Failed to initialize haptic device");
        return nullptr;
    }
    
    return haptic;
}

} // namespace InputBridge
