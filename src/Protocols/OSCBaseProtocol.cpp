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

bool OSCBaseProtocol::handle_osc_message(const char* path, const char* types, lo_arg** argv, int argc) {
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

    bool handled = false;

    if (match("/inputbridge/haptics/rumble", "haptic_rumble") && std::strcmp(types, "iiffi") == 0 && argc == 5) {
        handled = true;
        int slot = argv[1]->i;
        float low_freq = argv[2]->f;
        float high_freq = argv[3]->f;
        int duration_ms = argv[4]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low_freq, high_freq, (duration_ms < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
        });
    }
    else if (match("/inputbridge/haptics/force", "haptic_constant") && std::strcmp(types, "iifi") == 0 && argc == 4) {
        handled = true;
        int slot = argv[1]->i;
        float strength = argv[2]->f;
        int duration_int = argv[3]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    else if (match("/inputbridge/haptics/periodic", "haptic_periodic") && std::strcmp(types, "iiififfii") == 0 && argc == 9) {
        handled = true;
        int slot = argv[1]->i;
        HapticPeriodicType wave_type = PeriodicTypeFromIndex(argv[2]->i);
        float strength = argv[3]->f;
        int period = argv[4]->i;
        float magnitude = argv[5]->f;
        float offset = argv[6]->f;
        int phase = argv[7]->i;
        int duration_int = argv[8]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, wave_type, strength, period, magnitude, offset, phase, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    // Legacy: no wave_type argument — defaults to Sine.
    else if (match("/inputbridge/haptics/periodic", "haptic_periodic") && std::strcmp(types, "iififfii") == 0 && argc == 8) {
        handled = true;
        int slot = argv[1]->i;
        float strength = argv[2]->f;
        int period = argv[3]->i;
        float magnitude = argv[4]->f;
        float offset = argv[5]->f;
        int phase = argv[6]->i;
        int duration_int = argv[7]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, HapticPeriodicType::Sine, strength, period, magnitude, offset, phase, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    else if (match("/inputbridge/haptics/condition", "haptic_condition") && std::strcmp(types, "iiiffffffi") == 0 && argc == 10) {
        handled = true;
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
        handled = true;
        int gain = argv[1]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->SetGain(gain);
        });
    }
    else if (path_sv.find("/inputbridge/haptics/dualsense/trigger/") != std::string_view::npos) {
        handled = true;
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
    // RPM LED meter — /inputbridge/wheel/led_rpm f  (value 0.0 – 1.0)
    // Sets the RPM LED bar on all connected RPM-capable steering wheels.
    else if (path_sv == "/inputbridge/wheel/led_rpm" && std::strcmp(types, "f") == 0 && argc == 1) {
        handled = true;
        float rpm_percent = argv[0]->f;
        if (rpm_percent < 0.0f) rpm_percent = 0.0f;
        if (rpm_percent > 1.0f) rpm_percent = 1.0f;

        auto& deviceManager = DeviceManager::GetInstance();
        for (const auto& wheel : deviceManager.GetWheelRPMDevices()) {
            wheel->setRPM(rpm_percent);
        }
    }

    // ── Subchannel paths: /inputbridge/haptics/<effect>/<slot> ───────────────
    // The slot is encoded as the trailing decimal path component instead of
    // being passed as a message argument.  This lets hosts that can send only
    // one OSC message per frame per path (e.g. Resonite) address multiple
    // independent slots by using distinct paths:
    //
    //   /inputbridge/haptics/rumble/0   iffi  (id, low_freq, high_freq, dur)
    //   /inputbridge/haptics/rumble/1   iffi  (id, low_freq, high_freq, dur)
    //
    // Custom-protocol field IDs are matched against the base path (without the
    // trailing /N), so user-defined paths like "/my/rumble/0" also work.

    const auto last_slash = path_sv.rfind('/');
    if (last_slash == std::string_view::npos) return handled;
    const auto tail = path_sv.substr(last_slash + 1);
    if (tail.empty()) return handled;
    bool all_digits = true;
    for (char c : tail) { if (c < '0' || c > '9') { all_digits = false; break; } }
    if (!all_digits) return handled;

    int slot = 0;
    for (char c : tail) slot = slot * 10 + (c - '0');
    const std::string_view base = path_sv.substr(0, last_slash);

    // Re-run field-id lookup against the base path (without the /N suffix).
    std::string baseFieldId;
    if (def) {
        for (const auto& field : def->fields) {
            if (field.enabled && field.oscPath == base) {
                baseFieldId = field.fieldId;
                break;
            }
        }
    }
    auto matchBase = [&](const char* legacyPath, const char* fid) {
        return (baseFieldId == fid) || (baseFieldId.empty() && base == legacyPath);
    };

    // /inputbridge/haptics/rumble/N  iffi  (id, low_freq, high_freq, duration_ms)
    if (matchBase("/inputbridge/haptics/rumble", "haptic_rumble")
        && std::strcmp(types, "iffi") == 0 && argc == 4) {
        handled = true;
        const float low      = argv[1]->f;
        const float high     = argv[2]->f;
        const int   duration = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low, high, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/force/N  ifi  (id, strength, duration_ms)
    else if (matchBase("/inputbridge/haptics/force", "haptic_constant")
        && std::strcmp(types, "ifi") == 0 && argc == 3) {
        handled = true;
        const float strength = argv[1]->f;
        const int   duration = argv[2]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/periodic/N  iififfii  (id, wave_type, strength, period, magnitude, offset, phase, duration_ms)
    else if (matchBase("/inputbridge/haptics/periodic", "haptic_periodic")
        && std::strcmp(types, "iififfii") == 0 && argc == 8) {
        handled = true;
        const HapticPeriodicType wave_type = PeriodicTypeFromIndex(argv[1]->i);
        const float strength  = argv[2]->f;
        const int   period    = argv[3]->i;
        const float magnitude = argv[4]->f;
        const float offset    = argv[5]->f;
        const int   phase     = argv[6]->i;
        const int   duration  = argv[7]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, wave_type, strength, period, magnitude, offset, phase,
                (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // Legacy: /inputbridge/haptics/periodic/N  ififfii  — no wave_type, defaults to Sine
    else if (matchBase("/inputbridge/haptics/periodic", "haptic_periodic")
        && std::strcmp(types, "ififfii") == 0 && argc == 7) {
        handled = true;
        const float strength  = argv[1]->f;
        const int   period    = argv[2]->i;
        const float magnitude = argv[3]->f;
        const float offset    = argv[4]->f;
        const int   phase     = argv[5]->i;
        const int   duration  = argv[6]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, HapticPeriodicType::Sine, strength, period, magnitude, offset, phase,
                (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/condition/N  iiffffffi  (id, condition_type, rsat, lsat, rcoeff, lcoeff, deadband, center, duration_ms)
    else if (matchBase("/inputbridge/haptics/condition", "haptic_condition")
        && std::strcmp(types, "iiffffffi") == 0 && argc == 9) {
        handled = true;
        const HapticConditionType ctype  = ConditionTypeFromIndex(argv[1]->i);
        const float right_sat  = argv[2]->f;
        const float left_sat   = argv[3]->f;
        const float right_coeff = argv[4]->f;
        const float left_coeff  = argv[5]->f;
        const float deadband   = argv[6]->f;
        const float center     = argv[7]->f;
        const int   duration   = argv[8]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayCondition(slot, ctype, right_sat, left_sat, right_coeff, left_coeff, deadband, center,
                (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // Note: /haptic/gain has no slot dimension — no subchannel variant is defined.

    return handled;
}