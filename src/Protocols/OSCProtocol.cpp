#include "Protocols/OSCProtocol.h"
#include "Devices/DeviceManager.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/SteeringWheelHaptics.h"
#include <string_view>
#include <cstring>

OSCProtocol::OSCProtocol() {
    OSCServer::GetInstance().SetHandler([this](const char* path, const char* types, lo_arg** argv, int argc) {
        handle_osc_message(path, types, argv, argc);
    });
}

OSCProtocol::~OSCProtocol() {
    OSCServer::GetInstance().SetHandler(nullptr);
}

std::string OSCProtocol::format(const std::string &address, float value) { return ""; }
std::string OSCProtocol::format(const std::string &address, int value) { return ""; }
std::string OSCProtocol::format(const std::string &address, const std::string &value) { return ""; }
std::string OSCProtocol::format_wheel(float wheel, float brake, float throttle, float pitch, float roll) { return ""; }
void OSCProtocol::parse(const std::string& message) { }

void OSCProtocol::handle_osc_message(const char* path, const char* types, lo_arg** argv, int argc) {
    std::string_view path_sv(path);

    // Incoming haptics messages
    // Example: /inputbridge/haptics/rumble i f f i (deviceId, low_freq, high_freq, duration_ms)
    if (path_sv == "/inputbridge/haptics/rumble" && std::strcmp(types, "iffi") == 0 && argc == 4) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        float low_freq = argv[1]->f;
        float high_freq = argv[2]->f;
        int duration_ms = argv[3]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                gamepad->Rumble(low_freq, high_freq, (duration_ms < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
            }
        }
    }
    // Example: /inputbridge/haptics/force i f i (deviceId, strength, duration_ms)
    else if (path_sv == "/inputbridge/haptics/force" && std::strcmp(types, "ifi") == 0 && argc == 3) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        float strength = argv[1]->f;
        int duration_int = argv[2]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
             if (auto* wheel = dynamic_cast<SteeringWheelHaptics*>(haptic_dev)) {
                wheel->PlayConstant(strength, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
            }
        }
    }
    // Example: /inputbridge/haptics/periodic i f i f f i i (deviceId, strength, period, magnitude, offset, phase, duration_ms)
    else if (path_sv == "/inputbridge/haptics/periodic" && std::strcmp(types, "ififfii") == 0 && argc == 7) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        float strength = argv[1]->f;
        int period = argv[2]->i;
        float magnitude = argv[3]->f;
        float offset = argv[4]->f;
        int phase = argv[5]->i;
        int duration_int = argv[6]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
             if (auto* wheel = dynamic_cast<SteeringWheelHaptics*>(haptic_dev)) {
                wheel->PlayPeriodic(strength, period, magnitude, offset, phase, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
            }
        }
    }
    // Example: /inputbridge/haptics/condition i f f f f f f i (deviceId, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms)
    else if (path_sv == "/inputbridge/haptics/condition" && std::strcmp(types, "iffffffi") == 0 && argc == 8) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        float right_sat = argv[1]->f;
        float left_sat = argv[2]->f;
        float right_coeff = argv[3]->f;
        float left_coeff = argv[4]->f;
        float deadband = argv[5]->f;
        float center = argv[6]->f;
        int duration_int = argv[7]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
             if (auto* wheel = dynamic_cast<SteeringWheelHaptics*>(haptic_dev)) {
                wheel->PlayCondition(right_sat, left_sat, right_coeff, left_coeff, deadband, center, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
            }
        }
    }
    // Example: /inputbridge/haptics/gain i i (deviceId, gain)
    else if (path_sv == "/inputbridge/haptics/gain" && std::strcmp(types, "ii") == 0 && argc == 2) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        int gain = argv[1]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
             if (auto* wheel = dynamic_cast<SteeringWheelHaptics*>(haptic_dev)) {
                wheel->SetGain(gain);
            }
        }
    }
    // DualSense Trigger Effects
    // Example: /inputbridge/haptics/dualsense/trigger/left/feedback i i i (deviceId, position, strength)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/feedback" && std::strcmp(types, "iii") == 0 && argc == 3) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        int position = argv[1]->i;
        int strength = argv[2]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                params["position"] = position;
                params["strength"] = strength;
                gamepad->SendDualSenseTrigger("left", "feedback", params);
            }
        }
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/feedback" && std::strcmp(types, "iii") == 0 && argc == 3) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        int position = argv[1]->i;
        int strength = argv[2]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                params["position"] = position;
                params["strength"] = strength;
                gamepad->SendDualSenseTrigger("right", "feedback", params);
            }
        }
    }
    // Example: /inputbridge/haptics/dualsense/trigger/left/weapon i i i i (deviceId, start_position, end_position, strength)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/weapon" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        int start_pos = argv[1]->i;
        int end_pos = argv[2]->i;
        int strength = argv[3]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                params["start_position"] = start_pos;
                params["end_position"] = end_pos;
                params["strength"] = strength;
                gamepad->SendDualSenseTrigger("left", "weapon", params);
            }
        }
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/weapon" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        int start_pos = argv[1]->i;
        int end_pos = argv[2]->i;
        int strength = argv[3]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                params["start_position"] = start_pos;
                params["end_position"] = end_pos;
                params["strength"] = strength;
                gamepad->SendDualSenseTrigger("right", "weapon", params);
            }
        }
    }
    // Example: /inputbridge/haptics/dualsense/trigger/left/vibration i i i i (deviceId, position, amplitude, frequency)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/vibration" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        int position = argv[1]->i;
        int amplitude = argv[2]->i;
        int frequency = argv[3]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                params["position"] = position;
                params["amplitude"] = amplitude;
                params["frequency"] = frequency;
                gamepad->SendDualSenseTrigger("left", "vibration", params);
            }
        }
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/vibration" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        int position = argv[1]->i;
        int amplitude = argv[2]->i;
        int frequency = argv[3]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                params["position"] = position;
                params["amplitude"] = amplitude;
                params["frequency"] = frequency;
                gamepad->SendDualSenseTrigger("right", "vibration", params);
            }
        }
    }
    // Example: /inputbridge/haptics/dualsense/trigger/left/off i (deviceId)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/off" && std::strcmp(types, "i") == 0 && argc == 1) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                gamepad->SendDualSenseTrigger("left", "off", params);
            }
        }
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/off" && std::strcmp(types, "i") == 0 && argc == 1) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::map<std::string, int> params;
                gamepad->SendDualSenseTrigger("right", "off", params);
            }
        }
    }
}
