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
#include <string>
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

    // DualSense adaptive trigger senders, one per effect shape, parameterized
    // by trigger side ("left"/"right"). Defined here so they're available to
    // the per-side/per-effect match() branches below (one Protocol Editor
    // field per side x effect, see "Adaptive Trigger" category).
    auto sendMultiplePositionFeedback = [&](const char* trig) {
        // iiiiiiiiiii = deviceId + 10 per-position strengths
        if (std::strcmp(types, "iiiiiiiiiii") != 0 || argc != 11) return;
        std::map<std::string, int> params;
        for (int i = 0; i < 10; ++i) {
            params["strength_" + std::to_string(i)] = ClampDSMultiStrength(argv[i + 1]->i, path_sv);
        }
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "multi_position_feedback", params);
        });
    };
    auto sendMultiplePositionVibration = [&](const char* trig) {
        // iiiiiiiiiiii = deviceId + frequency + 10 per-position amplitudes
        if (std::strcmp(types, "iiiiiiiiiiii") != 0 || argc != 12) return;
        std::map<std::string, int> params;
        params["frequency"] = ClampDSFrequency(argv[1]->i, path_sv);
        for (int i = 0; i < 10; ++i) {
            params["amplitude_" + std::to_string(i)] = ClampDSMultiAmplitude(argv[i + 2]->i, path_sv);
        }
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "multi_position_vibration", params);
        });
    };
    auto sendFeedback = [&](const char* trig) {
        if (std::strcmp(types, "iii") != 0 || argc != 3) return;
        std::map<std::string, int> params;
        params["position"] = ClampDSPosition(argv[1]->i, path_sv);
        params["strength"] = ClampDSStrength(argv[2]->i, path_sv);
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "feedback", params);
        });
    };
    auto sendWeapon = [&](const char* trig) {
        if (std::strcmp(types, "iiii") != 0 || argc != 4) return;
        std::map<std::string, int> params;
        params["start_position"] = ClampDSStartPos(argv[1]->i, path_sv);
        params["end_position"]   = ClampDSEndPos(argv[2]->i,   path_sv);
        params["strength"]       = ClampDSStrength(argv[3]->i, path_sv);
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "weapon", params);
        });
    };
    auto sendVibration = [&](const char* trig) {
        if (std::strcmp(types, "iiii") != 0 || argc != 4) return;
        std::map<std::string, int> params;
        params["position"]  = ClampDSPosition(argv[1]->i,  path_sv);
        params["amplitude"] = ClampDSAmplitude(argv[2]->i, path_sv);
        params["frequency"] = ClampDSFrequency(argv[3]->i, path_sv);
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "vibration", params);
        });
    };
    auto sendSlopeFeedback = [&](const char* trig) {
        if (std::strcmp(types, "iiiii") != 0 || argc != 5) return;
        std::map<std::string, int> params;
        params["start_position"] = ClampDSSlopePos(argv[1]->i,       path_sv);
        params["end_position"]   = ClampDSSlopeEndPos(argv[2]->i,    path_sv);
        params["start_strength"] = ClampDSSlopeStrength(argv[3]->i,  path_sv);
        params["end_strength"]   = ClampDSSlopeStrength(argv[4]->i,  path_sv);
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "slope_feedback", params);
        });
    };
    auto sendBow = [&](const char* trig) {
        if (std::strcmp(types, "iiiii") != 0 || argc != 5) return;
        std::map<std::string, int> params;
        params["start_position"] = ClampDSBowPos(argv[1]->i,    path_sv);
        params["end_position"]   = ClampDSBowPos(argv[2]->i,    path_sv);
        params["strength"]       = ClampDSStrength(argv[3]->i,  path_sv);
        params["snap_force"]     = ClampDSSnapForce(argv[4]->i, path_sv);
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "bow", params);
        });
    };
    auto sendGalloping = [&](const char* trig) {
        if (std::strcmp(types, "iiiiii") != 0 || argc != 6) return;
        std::map<std::string, int> params;
        params["start_position"] = ClampDSGallopingPos(argv[1]->i, path_sv);
        params["end_position"]   = ClampDSGallopingPos(argv[2]->i, path_sv);
        params["first_foot"]     = ClampDSFirstFoot(argv[3]->i,    path_sv);
        params["second_foot"]    = ClampDSSecondFoot(argv[4]->i,   path_sv);
        params["frequency"]      = ClampDSFrequency(argv[5]->i,    path_sv);
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "galloping", params);
        });
    };
    auto sendMachine = [&](const char* trig) {
        if (std::strcmp(types, "iiiiiii") != 0 || argc != 7) return;
        std::map<std::string, int> params;
        params["start_position"] = ClampDSMachinePos(argv[1]->i,  path_sv);
        params["end_position"]   = ClampDSMachinePos(argv[2]->i,  path_sv);
        params["amplitude_a"]    = ClampDSAmplitudeAB(argv[3]->i, path_sv);
        params["amplitude_b"]    = ClampDSAmplitudeAB(argv[4]->i, path_sv);
        params["frequency"]      = ClampDSFrequency(argv[5]->i,   path_sv);
        params["period"]         = ClampDSPeriod(argv[6]->i,      path_sv);
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "machine", params);
        });
    };
    auto sendOff = [&](const char* trig) {
        DispatchHapticCommand<GamepadHaptics>([&, trig](GamepadHaptics* gamepad) {
            gamepad->SendDualSenseTrigger(trig, "off", {});
        });
    };

    bool handled = false;

    // /haptic/rumble  iiffi  (deviceId, slot, low_freq, high_freq, duration_ms)
    if (match("/haptic/rumble", "haptic_rumble") && std::strcmp(types, "iiffi") == 0 && argc == 5) {
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
    // /haptic/force  iifi  (deviceId, slot, strength, duration_ms)
    else if (match("/haptic/force", "haptic_constant") && std::strcmp(types, "iifi") == 0 && argc == 4) {
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
    // /haptic/periodic  iiififfii  (deviceId, slot, wave_type, strength, period, magnitude, offset, phase, duration_ms)
    else if (match("/haptic/periodic", "haptic_periodic") && std::strcmp(types, "iiififfii") == 0 && argc == 9) {
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
    else if (match("/haptic/periodic", "haptic_periodic") && std::strcmp(types, "iififfii") == 0 && argc == 8) {
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
    // /haptic/condition  iiiffffffi  (deviceId, slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms)
    else if (match("/haptic/condition", "haptic_condition") && std::strcmp(types, "iiiffffffi") == 0 && argc == 10) {
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
    // /haptic/gain  ii  (deviceId, gain)
    else if (match("/haptic/gain", "haptic_gain") && std::strcmp(types, "ii") == 0 && argc == 2) {
        handled = true;
        int gain = ClampGain(argv[1]->i, path_sv);
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->SetGain(gain);
        });
    }
    // DualSense Trigger Effects - one Protocol Editor field per trigger side
    // x effect (see "Adaptive Trigger" category), each independently
    // customizable since they're distinct addresses/arg signatures rather
    // than variations on one message shape. Addresses follow the same flat
    // /haptic/{name} single-segment convention as the other Haptic fields
    // above, rather than nesting left/right and the effect name as separate
    // path segments.
    //   feedback:  iii     (deviceId, position, strength)
    //   weapon:    iiii    (deviceId, start_position, end_position, strength)
    //   vibration: iiii    (deviceId, position, amplitude, frequency)
    //   multi_position_feedback:  iiiiiiiiiii  (deviceId, strength_0..strength_9)
    //   multi_position_vibration: iiiiiiiiiiii (deviceId, frequency, amplitude_0..amplitude_9)
    //   slope_feedback: iiiii (deviceId, start_position, end_position, start_strength, end_strength)
    //   bow:       iiiii   (deviceId, start_position, end_position, strength, snap_force)
    //   galloping: iiiiii  (deviceId, start_position, end_position, first_foot, second_foot, frequency)
    //   machine:   iiiiiii (deviceId, start_position, end_position, amplitude_a, amplitude_b, frequency, period)
    //   off:       (no args required)
    else if (match("/haptic/dualsense/trigger/left/feedback",   "ds_trigger_left_feedback"))   { handled = true; sendFeedback("left"); }
    else if (match("/haptic/dualsense/trigger/right/feedback",  "ds_trigger_right_feedback"))  { handled = true; sendFeedback("right"); }
    else if (match("/haptic/dualsense/trigger/left/weapon",     "ds_trigger_left_weapon"))     { handled = true; sendWeapon("left"); }
    else if (match("/haptic/dualsense/trigger/right/weapon",    "ds_trigger_right_weapon"))    { handled = true; sendWeapon("right"); }
    else if (match("/haptic/dualsense/trigger/left/vibration",  "ds_trigger_left_vibration"))  { handled = true; sendVibration("left"); }
    else if (match("/haptic/dualsense/trigger/right/vibration", "ds_trigger_right_vibration")) { handled = true; sendVibration("right"); }
    else if (match("/haptic/dualsense/trigger/left/multi_position_feedback",  "ds_trigger_left_multi_position_feedback"))  { handled = true; sendMultiplePositionFeedback("left"); }
    else if (match("/haptic/dualsense/trigger/right/multi_position_feedback", "ds_trigger_right_multi_position_feedback")) { handled = true; sendMultiplePositionFeedback("right"); }
    else if (match("/haptic/dualsense/trigger/left/multi_position_vibration",  "ds_trigger_left_multi_position_vibration"))  { handled = true; sendMultiplePositionVibration("left"); }
    else if (match("/haptic/dualsense/trigger/right/multi_position_vibration", "ds_trigger_right_multi_position_vibration")) { handled = true; sendMultiplePositionVibration("right"); }
    else if (match("/haptic/dualsense/trigger/left/slope_feedback",  "ds_trigger_left_slope_feedback"))  { handled = true; sendSlopeFeedback("left"); }
    else if (match("/haptic/dualsense/trigger/right/slope_feedback", "ds_trigger_right_slope_feedback")) { handled = true; sendSlopeFeedback("right"); }
    else if (match("/haptic/dualsense/trigger/left/bow",        "ds_trigger_left_bow"))        { handled = true; sendBow("left"); }
    else if (match("/haptic/dualsense/trigger/right/bow",       "ds_trigger_right_bow"))       { handled = true; sendBow("right"); }
    else if (match("/haptic/dualsense/trigger/left/galloping",  "ds_trigger_left_galloping"))  { handled = true; sendGalloping("left"); }
    else if (match("/haptic/dualsense/trigger/right/galloping", "ds_trigger_right_galloping")) { handled = true; sendGalloping("right"); }
    else if (match("/haptic/dualsense/trigger/left/machine",    "ds_trigger_left_machine"))    { handled = true; sendMachine("left"); }
    else if (match("/haptic/dualsense/trigger/right/machine",   "ds_trigger_right_machine"))   { handled = true; sendMachine("right"); }
    else if (match("/haptic/dualsense/trigger/left/off",        "ds_trigger_left_off"))        { handled = true; sendOff("left"); }
    else if (match("/haptic/dualsense/trigger/right/off",       "ds_trigger_right_off"))       { handled = true; sendOff("right"); }
    // /inputbridge/wheel/led_rpm  f  (rpm_percent 0.0–1.0)
    else if (path_sv == "/inputbridge/wheel/led_rpm" && std::strcmp(types, "f") == 0 && argc == 1) {
        handled = true;
        float rpm_percent = ClampNorm(argv[0]->f, "rpm_percent", path_sv);

        auto& deviceManager = DeviceManager::GetInstance();
        for (const auto& wheel : deviceManager.GetWheelRPMDevices()) {
            wheel->setRPM(rpm_percent);
        }
    }

    // ── Subchannel paths: /haptic/<effect>/<slot> ───────────────
    // The slot is encoded as the trailing decimal path component instead of
    // being passed as a message argument.  This lets hosts that can send only
    // one OSC message per frame per path (e.g. Resonite) address multiple
    // independent slots by using distinct paths:
    //
    //   /haptic/rumble/0   iffi  (id, low_freq, high_freq, dur)
    //   /haptic/rumble/1   iffi  (id, low_freq, high_freq, dur)
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

    // /haptic/rumble/N  iffi  (id, low_freq, high_freq, duration_ms)
    if (matchBase("/haptic/rumble", "haptic_rumble")
        && std::strcmp(types, "iffi") == 0 && argc == 4) {
        handled = true;
        float low      = ClampNorm(argv[1]->f, "low_freq",  path_sv);
        float high     = ClampNorm(argv[2]->f, "high_freq", path_sv);
        const int duration = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low, high, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /haptic/force/N  ifi  (id, strength, duration_ms)
    else if (matchBase("/haptic/force", "haptic_constant")
        && std::strcmp(types, "ifi") == 0 && argc == 3) {
        handled = true;
        float strength     = ClampStrength(argv[1]->f, "strength", path_sv);
        const int duration = argv[2]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /haptic/periodic/N  iififfii  (id, wave_type, strength, period, magnitude, offset, phase, duration_ms)
    else if (matchBase("/haptic/periodic", "haptic_periodic")
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
    // Legacy: /haptic/periodic/N  ififfii  - no wave_type, defaults to Sine
    else if (matchBase("/haptic/periodic", "haptic_periodic")
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
    // /haptic/condition/N  iiffffffi  (id, condition_type, rsat, lsat, rcoeff, lcoeff, deadband, center, duration_ms)
    else if (matchBase("/haptic/condition", "haptic_condition")
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