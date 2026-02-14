#pragma once
#include "DeviceState.h"
#include "../Haptics/HapticDevice.h"
#include <SDL3/SDL.h>
#include <memory>
#include <optional>

namespace InputBridge {

/**
 * @brief Result of device creation containing both device state and haptic device
 */
struct DeviceCreationResult {
    DeviceState state;
    std::unique_ptr<HapticDevice> haptic;
    
    DeviceCreationResult(DeviceState&& s, std::unique_ptr<HapticDevice>&& h)
        : state(std::move(s)), haptic(std::move(h)) {}
};

/**
 * @brief Factory class for creating device objects based on SDL device types
 * 
 * This class encapsulates the device creation logic, eliminating code duplication
 * in DeviceManager and making the device creation process more maintainable.
 * 
 * @example
 * auto result = DeviceFactory::CreateDevice(instance_id);
 * if (result) {
 *     // Use result->state and result->haptic
 * }
 */
class DeviceFactory {
public:
    /**
     * @brief Creates a device based on SDL joystick instance ID
     * @param instance_id The SDL joystick instance identifier
     * @return Optional containing DeviceCreationResult if successful, nullopt otherwise
     */
    static std::optional<DeviceCreationResult> CreateDevice(SDL_JoystickID instance_id);
    
private:
    /**
     * @brief Creates a steering wheel device
     */
    static std::optional<DeviceCreationResult> CreateWheelDevice(SDL_JoystickID instance_id);
    
    /**
     * @brief Creates a gamepad device
     */
    static std::optional<DeviceCreationResult> CreateGamepadDevice(SDL_JoystickID instance_id);
    
    /**
     * @brief Creates a generic device (flight stick, arcade stick, etc.)
     */
    static std::optional<DeviceCreationResult> CreateGenericDevice(SDL_JoystickID instance_id);
    
    /**
     * @brief Populates common device state fields
     * @param joystick SDL joystick handle (non-owning)
     * @param instance_id SDL joystick instance ID
     * @return Populated DeviceState with common fields
     */
    static DeviceState CreateBaseDeviceState(SDL_Joystick* joystick, SDL_JoystickID instance_id);
    
    /**
     * @brief Attempts to create a haptic device for a joystick
     * @param joystick SDL joystick handle
     * @param device_type Type of device (for selecting correct haptic implementation)
     * @return Unique pointer to HapticDevice, or nullptr if haptics not supported
     */
    static std::unique_ptr<HapticDevice> CreateHapticDevice(SDL_Joystick* joystick, 
                                                             SDL_JoystickType device_type);
};

} // namespace InputBridge
