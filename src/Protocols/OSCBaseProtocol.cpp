#include "Protocols/OSCBaseProtocol.h"
#include "Network/OSCServer.h"
#include "Devices/DeviceManager.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/SteeringWheelHaptics.h"
#include <cstring>
#include <string_view>
#include <map>

void OSCBaseProtocol::handle_osc_message(const char* path, const char* types, lo_arg** argv, int argc) {
    std::string_view path_sv(path);

    std::string activeId = ProtocolManager::GetInstance().GetActiveInputProtocolId();
    const ProtocolDefinition* def = nullptr;
    if (!activeId.empty()) def = ProtocolRegistry::GetInstance().FindById(activeId);

    std::string fieldId;
    if (def) {
        for (const auto& field : def->fields) {
            if (field.enabled && field.oscPath == path_sv) {
                fieldId = field.fieldId;
                break;
            }
        }
    }

    // Helper to check if we matched a field or if we should fallback to legacy path
    auto match = [&](const char* legacyPath, const char* fid) {
        return (fieldId == fid) || (fieldId.empty() && path_sv == legacyPath);
    };

    if (match("/inputbridge/haptics/rumble", "haptic_rumble") && std::strcmp(types, "iffi") == 0 && argc == 4) {
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
    else if (match("/inputbridge/haptics/force", "haptic_constant") && std::strcmp(types, "ifi") == 0 && argc == 3) {
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
    else if (match("/inputbridge/haptics/periodic", "haptic_periodic") && std::strcmp(types, "ififfii") == 0 && argc == 7) {
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
    else if (match("/inputbridge/haptics/condition", "haptic_condition") && std::strcmp(types, "iffffffi") == 0 && argc == 8) {
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
    else if (match("/inputbridge/haptics/gain", "haptic_gain") && std::strcmp(types, "ii") == 0 && argc == 2) {
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
    else if (path_sv.find("/inputbridge/haptics/dualsense/trigger/") != std::string_view::npos) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);

        if (haptic_dev) {
            if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_dev)) {
                std::string trigger = (path_sv.find("/left/") != std::string_view::npos) ? "left" : "right";
                std::string effect = "off";
                std::map<std::string, int> params;

                if (path_sv.ends_with("/feedback") && std::strcmp(types, "iii") == 0 && argc == 3) {
                    effect = "feedback";
                    params["position"] = argv[1]->i;
                    params["strength"] = argv[2]->i;
                } else if (path_sv.ends_with("/weapon") && std::strcmp(types, "iiii") == 0 && argc == 4) {
                    effect = "weapon";
                    params["start_position"] = argv[1]->i;
                    params["end_position"] = argv[2]->i;
                    params["strength"] = argv[3]->i;
                } else if (path_sv.ends_with("/vibration") && std::strcmp(types, "iiii") == 0 && argc == 4) {
                    effect = "vibration";
                    params["position"] = argv[1]->i;
                    params["amplitude"] = argv[2]->i;
                    params["frequency"] = argv[3]->i;
                } else if (path_sv.ends_with("/off")) {
                    effect = "off";
                }

                if (effect != "off" || path_sv.ends_with("/off")) {
                    gamepad->SendDualSenseTrigger(trigger.c_str(), effect.c_str(), params);
                }
            }
        }
    }
}
