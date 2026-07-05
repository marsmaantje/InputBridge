#include "Protocols/OSCBaseProtocol.h"
#include "Protocols/OSCValidation.h"
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
    using namespace OscValidation;

    if (!CheckPointers(path, types, argv, argc))
        return false;

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

    auto match = [&](const char* legacyPath, const char* fid) {
        return (fieldId == fid) || (fieldId.empty() && path_sv == legacyPath);
    };

    bool handled = false;

    // /inputbridge/haptics/rumble  iiffi  (deviceId, slot, low_freq, high_freq, duration_ms)
    if (match("/inputbridge/haptics/rumble", "haptic_rumble") && std::strcmp(types, "iiffi") == 0 && argc == 5) {
        handled = true;
        int   slot        = argv[1]->i;
        float low_freq    = argv[2]->f;
        float high_freq   = argv[3]->f;
        int   duration_ms = argv[4]->i;

        if (!ValidateSlot(slot, path_sv)) return handled;
        low_freq  = ClampNorm(low_freq,  "low_freq",  path_sv);
        high_freq = ClampNorm(high_freq, "high_freq", path_sv);

        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low_freq, high_freq,
                (duration_ms < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
        });
    }
    // /inputbridge/haptics/force  iifi  (deviceId, slot, strength, duration_ms)
    else if (match("/inputbridge/haptics/force", "haptic_constant") && std::strcmp(types, "iifi") == 0 && argc == 4) {
        handled = true;
        int   slot         = argv[1]->i;
        float strength     = argv[2]->f;
        int   duration_int = argv[3]->i;

        if (!ValidateSlot(slot, path_sv)) return handled;
        strength = ClampStrength(strength, "strength", path_sv);

        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength,
                (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    // /inputbridge/haptics/periodic  iiififfii  (deviceId, slot, wave_type, strength, period, magnitude, offset, phase, duration_ms)
    else if (match("/inputbridge/haptics/periodic", "haptic_periodic") && std::strcmp(types, "iiififfii") == 0 && argc == 9) {
        handled = true;
        int   slot         = argv[1]->i;
        int   wave_idx     = argv[2]->i;
        float strength     = argv[3]->f;
        int   period       = argv[4]->i;
        float magnitude    = argv[5]->f;
        float offset       = argv[6]->f;
        int   phase        = argv[7]->i;
        int   duration_int = argv[8]->i;

        if (!ValidateSlot(slot, path_sv))         return handled;
        if (!ValidateWaveType(wave_idx, path_sv)) return handled;
        strength  = ClampNorm(strength,  "strength",  path_sv);
        magnitude = ClampNorm(magnitude, "magnitude", path_sv);
        offset    = ClampSigned(offset,  "offset",    path_sv);

        HapticPeriodicType wave_type = PeriodicTypeFromIndex(wave_idx);
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, wave_type, strength, period, magnitude, offset, phase,
                (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    // Legacy periodic - no wave_type, defaults to Sine.
    else if (match("/inputbridge/haptics/periodic", "haptic_periodic") && std::strcmp(types, "iififfii") == 0 && argc == 8) {
        handled = true;
        int   slot         = argv[1]->i;
        float strength     = argv[2]->f;
        int   period       = argv[3]->i;
        float magnitude    = argv[4]->f;
        float offset       = argv[5]->f;
        int   phase        = argv[6]->i;
        int   duration_int = argv[7]->i;

        if (!ValidateSlot(slot, path_sv)) return handled;
        strength  = ClampNorm(strength,  "strength",  path_sv);
        magnitude = ClampNorm(magnitude, "magnitude", path_sv);
        offset    = ClampSigned(offset,  "offset",    path_sv);

        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, HapticPeriodicType::Sine, strength, period, magnitude, offset, phase,
                (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    // /inputbridge/haptics/condition  iiiffffffi  (deviceId, slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms)
    else if (match("/inputbridge/haptics/condition", "haptic_condition") && std::strcmp(types, "iiiffffffi") == 0 && argc == 10) {
        handled = true;
        int   slot           = argv[1]->i;
        int   cond_idx       = argv[2]->i;
        float right_sat      = argv[3]->f;
        float left_sat       = argv[4]->f;
        float right_coeff    = argv[5]->f;
        float left_coeff     = argv[6]->f;
        float deadband       = argv[7]->f;
        float center         = argv[8]->f;
        int   duration_int   = argv[9]->i;

        if (!ValidateSlot(slot, path_sv))              return handled;
        if (!ValidateConditionType(cond_idx, path_sv)) return handled;
        right_sat   = ClampNorm(right_sat,     "right_sat",   path_sv);
        left_sat    = ClampNorm(left_sat,       "left_sat",    path_sv);
        right_coeff = ClampSigned(right_coeff, "right_coeff", path_sv);
        left_coeff  = ClampSigned(left_coeff,  "left_coeff",  path_sv);
        deadband    = ClampNorm(deadband,      "deadband",    path_sv);
        center      = ClampSigned(center,      "center",      path_sv);

        HapticConditionType condition_type = ConditionTypeFromIndex(cond_idx);
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayCondition(slot, condition_type, right_sat, left_sat,
                right_coeff, left_coeff, deadband, center,
                (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    // /inputbridge/haptics/gain  ii  (deviceId, gain)
    else if (match("/inputbridge/haptics/gain", "haptic_gain") && std::strcmp(types, "ii") == 0 && argc == 2) {
        handled = true;
        int gain = ClampGain(argv[1]->i, path_sv);
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->SetGain(gain);
        });
    }
    // DualSense Trigger Effects
    // /inputbridge/haptics/dualsense/trigger/{left|right}/{feedback|weapon|vibration|bow|galloping|machine|off}
    //   feedback:  iii     (deviceId, position, strength)
    //   weapon:    iiii    (deviceId, start_position, end_position, strength)
    //   vibration: iiii    (deviceId, position, amplitude, frequency)
    //   bow:       iiiii   (deviceId, start_position, end_position, strength, snap_force)
    //   galloping: iiiiii  (deviceId, start_position, end_position, first_foot, second_foot, frequency)
    //   machine:   iiiiiii (deviceId, start_position, end_position, amplitude_a, amplitude_b, frequency, period)
    //   off:       (no args required)
    else if (path_sv.find("/inputbridge/haptics/dualsense/trigger/") != std::string_view::npos) {
        handled = true;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::string trigger = (path_sv.find("/left/") != std::string_view::npos) ? "left" : "right";
            std::string effect = "off";
            std::map<std::string, int> params;

            if (path_sv.ends_with("/feedback") && std::strcmp(types, "iii") == 0 && argc == 3) {
                effect = "feedback";
                params["position"] = ClampDSPosition(argv[1]->i, path_sv);
                params["strength"] = ClampDSStrength(argv[2]->i, path_sv);
            } else if (path_sv.ends_with("/weapon") && std::strcmp(types, "iiii") == 0 && argc == 4) {
                effect = "weapon";
                params["start_position"] = ClampDSStartPos(argv[1]->i, path_sv);
                params["end_position"]   = ClampDSEndPos(argv[2]->i,   path_sv);
                params["strength"]       = ClampDSStrength(argv[3]->i, path_sv);
            } else if (path_sv.ends_with("/vibration") && std::strcmp(types, "iiii") == 0 && argc == 4) {
                effect = "vibration";
                params["position"]  = ClampDSPosition(argv[1]->i,  path_sv);
                params["amplitude"] = ClampDSAmplitude(argv[2]->i, path_sv);
                params["frequency"] = ClampDSFrequency(argv[3]->i, path_sv);
            } else if (path_sv.ends_with("/bow") && std::strcmp(types, "iiiii") == 0 && argc == 5) {
                effect = "bow";
                params["start_position"] = ClampDSBowPos(argv[1]->i,     path_sv);
                params["end_position"]   = ClampDSBowPos(argv[2]->i,     path_sv);
                params["strength"]       = ClampDSStrength(argv[3]->i,   path_sv);
                params["snap_force"]     = ClampDSSnapForce(argv[4]->i,  path_sv);
            } else if (path_sv.ends_with("/galloping") && std::strcmp(types, "iiiiii") == 0 && argc == 6) {
                effect = "galloping";
                params["start_position"] = ClampDSGallopingPos(argv[1]->i, path_sv);
                params["end_position"]   = ClampDSGallopingPos(argv[2]->i, path_sv);
                params["first_foot"]     = ClampDSFirstFoot(argv[3]->i,   path_sv);
                params["second_foot"]    = ClampDSSecondFoot(argv[4]->i,  path_sv);
                params["frequency"]      = ClampDSFrequency(argv[5]->i,   path_sv);
            } else if (path_sv.ends_with("/machine") && std::strcmp(types, "iiiiiii") == 0 && argc == 7) {
                effect = "machine";
                params["start_position"] = ClampDSMachinePos(argv[1]->i,  path_sv);
                params["end_position"]   = ClampDSMachinePos(argv[2]->i,  path_sv);
                params["amplitude_a"]    = ClampDSAmplitudeAB(argv[3]->i, path_sv);
                params["amplitude_b"]    = ClampDSAmplitudeAB(argv[4]->i, path_sv);
                params["frequency"]      = ClampDSFrequency(argv[5]->i,   path_sv);
                params["period"]         = ClampDSPeriod(argv[6]->i,      path_sv);
            } else if (path_sv.ends_with("/off")) {
                effect = "off";
            }

            if (effect != "off" || path_sv.ends_with("/off")) {
                gamepad->SendDualSenseTrigger(trigger.c_str(), effect.c_str(), params);
            }
        });
    }
    // /inputbridge/wheel/led_rpm  f  (rpm_percent 0.0–1.0)
    else if (path_sv == "/inputbridge/wheel/led_rpm" && std::strcmp(types, "f") == 0 && argc == 1) {
        handled = true;
        float rpm_percent = ClampNorm(argv[0]->f, "rpm_percent", path_sv);

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

    if (!ValidateSlot(slot, path_sv)) return handled;

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
        float low      = ClampNorm(argv[1]->f, "low_freq",  path_sv);
        float high     = ClampNorm(argv[2]->f, "high_freq", path_sv);
        const int duration = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low, high, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/force/N  ifi  (id, strength, duration_ms)
    else if (matchBase("/inputbridge/haptics/force", "haptic_constant")
        && std::strcmp(types, "ifi") == 0 && argc == 3) {
        handled = true;
        float strength     = ClampStrength(argv[1]->f, "strength", path_sv);
        const int duration = argv[2]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/periodic/N  iififfii  (id, wave_type, strength, period, magnitude, offset, phase, duration_ms)
    else if (matchBase("/inputbridge/haptics/periodic", "haptic_periodic")
        && std::strcmp(types, "iififfii") == 0 && argc == 8) {
        handled = true;
        int wave_idx = argv[1]->i;
        if (!ValidateWaveType(wave_idx, path_sv)) return handled;
        float strength      = ClampNorm(argv[2]->f,    "strength",  path_sv);
        const int period    = argv[3]->i;
        float magnitude     = ClampNorm(argv[4]->f,    "magnitude", path_sv);
        float offset        = ClampSigned(argv[5]->f,  "offset",    path_sv);
        const int phase     = argv[6]->i;
        const int duration  = argv[7]->i;
        HapticPeriodicType wave_type = PeriodicTypeFromIndex(wave_idx);
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, wave_type, strength, period, magnitude, offset, phase,
                (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // Legacy: /inputbridge/haptics/periodic/N  ififfii  - no wave_type, defaults to Sine
    else if (matchBase("/inputbridge/haptics/periodic", "haptic_periodic")
        && std::strcmp(types, "ififfii") == 0 && argc == 7) {
        handled = true;
        float strength      = ClampNorm(argv[1]->f,    "strength",  path_sv);
        const int period    = argv[2]->i;
        float magnitude     = ClampNorm(argv[3]->f,    "magnitude", path_sv);
        float offset        = ClampSigned(argv[4]->f,  "offset",    path_sv);
        const int phase     = argv[5]->i;
        const int duration  = argv[6]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayPeriodic(slot, HapticPeriodicType::Sine, strength, period, magnitude, offset, phase,
                (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/condition/N  iiffffffi  (id, condition_type, rsat, lsat, rcoeff, lcoeff, deadband, center, duration_ms)
    else if (matchBase("/inputbridge/haptics/condition", "haptic_condition")
        && std::strcmp(types, "iiffffffi") == 0 && argc == 9) {
        handled = true;
        int cond_idx = argv[1]->i;
        if (!ValidateConditionType(cond_idx, path_sv)) return handled;
        float right_sat     = ClampNorm(argv[2]->f,    "right_sat",   path_sv);
        float left_sat      = ClampNorm(argv[3]->f,    "left_sat",    path_sv);
        float right_coeff   = ClampSigned(argv[4]->f,  "right_coeff", path_sv);
        float left_coeff    = ClampSigned(argv[5]->f,  "left_coeff",  path_sv);
        float deadband      = ClampNorm(argv[6]->f,    "deadband",    path_sv);
        float center        = ClampSigned(argv[7]->f,  "center",      path_sv);
        const int duration  = argv[8]->i;
        HapticConditionType ctype = ConditionTypeFromIndex(cond_idx);
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayCondition(slot, ctype, right_sat, left_sat, right_coeff, left_coeff, deadband, center,
                (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // Note: /haptic/gain has no slot dimension - no subchannel variant is defined.

    return handled;
}