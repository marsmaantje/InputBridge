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

namespace {
    template<typename HapticType, typename Func>
    void DispatchHapticCommand(Func func) {
        int deviceId = OSCServer::GetInstance().GetSelectedDevice();
        auto& deviceManager = DeviceManager::GetInstance();
        HapticDevice* haptic_dev = deviceManager.GetHapticDevice(deviceId);
        if (haptic_dev) {
            if (auto* specific_dev = dynamic_cast<HapticType*>(haptic_dev)) {
                func(specific_dev);
            }
        }
    }
}

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

    if (match("/inputbridge/haptics/rumble", "haptic_rumble") && std::strcmp(types, "iiffi") == 0 && argc == 5) {
        int slot = argv[1]->i;
        float low_freq = argv[2]->f;
        float high_freq = argv[3]->f;
        int duration_ms = argv[4]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low_freq, high_freq, (duration_ms < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
        });
    }
    else if (match("/inputbridge/haptics/force", "haptic_constant") && std::strcmp(types, "iifi") == 0 && argc == 4) {
        int slot = argv[1]->i;
        float strength = argv[2]->f;
        int duration_int = argv[3]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    else if (match("/inputbridge/haptics/periodic", "haptic_periodic") && std::strcmp(types, "iififfii") == 0 && argc == 8) {
        int slot = argv[1]->i;
        float strength = argv[2]->f;
        int period = argv[3]->i;
        float magnitude = argv[4]->f;
        float offset = argv[5]->f;
        int phase = argv[6]->i;
        int duration_int = argv[7]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, strength, period, magnitude, offset, phase, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    else if (match("/inputbridge/haptics/condition", "haptic_condition") && std::strcmp(types, "iiiffffffi") == 0 && argc == 10) {
        int slot = argv[1]->i;
        HapticConditionType condition_type = ConditionTypeFromIndex(argv[2]->i);
        float right_sat = argv[3]->f;
        float left_sat = argv[4]->f;
        float right_coeff = argv[5]->f;
        float left_coeff = argv[6]->f;
        float deadband = argv[7]->f;
        float center = argv[8]->f;
        int duration_int = argv[9]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayCondition(slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    else if (match("/inputbridge/haptics/gain", "haptic_gain") && std::strcmp(types, "ii") == 0 && argc == 2) {
        int gain = argv[1]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->SetGain(gain);
        });
    }
    else if (path_sv.find("/inputbridge/haptics/dualsense/trigger/") != std::string_view::npos) {
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
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
        });
    }
}