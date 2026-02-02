#include "Protocols/OSCProtocol.h"
#include "Devices/DeviceManager.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/SteeringWheelHaptics.h"
#include <iostream>
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
        int deviceId = argv[0]->i;
        float low_freq = argv[1]->f;
        float high_freq = argv[2]->f;
        int duration_ms = argv[3]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                gamepad->Rumble(low_freq, high_freq, duration_ms);
            }
        }
    }
    // Example: /inputbridge/haptics/force i f i (deviceId, strength, duration_ms)
    else if (path_sv == "/inputbridge/haptics/force" && std::strcmp(types, "ifi") == 0 && argc == 3) {
        int deviceId = argv[0]->i;
        float strength = argv[1]->f;
        int duration_ms = argv[2]->i;

        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
             if (auto* wheel = dynamic_cast<SteeringWheelHaptics*>(haptic_dev)) {
                wheel->PlayConstant(strength, duration_ms);
            }
        }
    }
}
