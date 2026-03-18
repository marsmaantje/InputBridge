#include "Protocols/OSCProtocol.h"
#include "Devices/DeviceManager.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/SteeringWheelHaptics.h"
#include "Haptics/HapticDevice.h"
#include <string_view>
#include <cstring>
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

OSCProtocol::OSCProtocol() {
    OSCServer::GetInstance().SetHandler([this](const char* path, const char* types, lo_arg** argv, int argc) {
        handle_osc_message(path, types, argv, argc);
    });
}

OSCProtocol::~OSCProtocol() {
    // OSCServer is a static singleton constructed after ProtocolManager and
    // therefore destroyed before it.  If ProtocolManager::Clear() was not
    // called during Shutdown(), our destructor could run after OSCServer's
    // destructor has already finished — calling SetHandler on a dead object.
    // IsDestroyed() guards against that scenario.
    if (!OSCServer::IsDestroyed()) {
        OSCServer::GetInstance().SetHandler(nullptr);
    }
}

std::string OSCProtocol::format(const std::string &address, float value) { return ""; }
std::string OSCProtocol::format(const std::string &address, int value) { return ""; }
std::string OSCProtocol::format(const std::string &address, const std::string &value) { return ""; }
std::string OSCProtocol::format_wheel(const std::map<std::string, float>& values) { return ""; }
void OSCProtocol::parse(const std::string& message) { }

void OSCProtocol::handle_osc_message(const char* path, const char* types, lo_arg** argv, int argc) {
    std::string_view path_sv(path);

    // Incoming haptics messages
    // Example: /inputbridge/haptics/rumble i i f f i (deviceId, slot, low_freq, high_freq, duration_ms)
    if (path_sv == "/inputbridge/haptics/rumble" && std::strcmp(types, "iiffi") == 0 && argc == 5) {
        int slot = argv[1]->i;
        float low_freq = argv[2]->f;
        float high_freq = argv[3]->f;
        int duration_ms = argv[4]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low_freq, high_freq, (duration_ms < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
        });
    }
    // Example: /inputbridge/haptics/force i i f i (deviceId, slot, strength, duration_ms)
    else if (path_sv == "/inputbridge/haptics/force" && std::strcmp(types, "iifi") == 0 && argc == 4) {
        int slot = argv[1]->i;
        float strength = argv[2]->f;
        int duration_int = argv[3]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength, (duration_int < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_int);
        });
    }
    // Example: /inputbridge/haptics/periodic i i i f i f f i i (deviceId, slot, wave_type, strength, period, magnitude, offset, phase, duration_ms)
    // wave_type: 0=Sine, 1=Triangle, 2=SawtoothUp, 3=SawtoothDown
    else if (path_sv == "/inputbridge/haptics/periodic" && std::strcmp(types, "iiififfii") == 0 && argc == 9) {
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
    else if (path_sv == "/inputbridge/haptics/periodic" && std::strcmp(types, "iififfii") == 0 && argc == 8) {
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
    // Example: /inputbridge/haptics/condition i i i f f f f f f i (deviceId, slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms)
    // condition_type: 0=Spring, 1=Damper, 2=Inertia, 3=Friction
    else if (path_sv == "/inputbridge/haptics/condition" && std::strcmp(types, "iiiffffffi") == 0 && argc == 10) {
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
    // Example: /inputbridge/haptics/gain i i (deviceId, gain)
    else if (path_sv == "/inputbridge/haptics/gain" && std::strcmp(types, "ii") == 0 && argc == 2) {
        int gain = argv[1]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->SetGain(gain);
        });
    }
    // DualSense Trigger Effects
    // Example: /inputbridge/haptics/dualsense/trigger/left/feedback i i i (deviceId, position, strength)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/feedback" && std::strcmp(types, "iii") == 0 && argc == 3) {
        int position = argv[1]->i;
        int strength = argv[2]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            params["position"] = position;
            params["strength"] = strength;
            gamepad->SendDualSenseTrigger("left", "feedback", params);
        });
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/feedback" && std::strcmp(types, "iii") == 0 && argc == 3) {
        int position = argv[1]->i;
        int strength = argv[2]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            params["position"] = position;
            params["strength"] = strength;
            gamepad->SendDualSenseTrigger("right", "feedback", params);
        });
    }
    // Example: /inputbridge/haptics/dualsense/trigger/left/weapon i i i i (deviceId, start_position, end_position, strength)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/weapon" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int start_pos = argv[1]->i;
        int end_pos = argv[2]->i;
        int strength = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            params["start_position"] = start_pos;
            params["end_position"] = end_pos;
            params["strength"] = strength;
            gamepad->SendDualSenseTrigger("left", "weapon", params);
        });
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/weapon" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int start_pos = argv[1]->i;
        int end_pos = argv[2]->i;
        int strength = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            params["start_position"] = start_pos;
            params["end_position"] = end_pos;
            params["strength"] = strength;
            gamepad->SendDualSenseTrigger("right", "weapon", params);
        });
    }
    // Example: /inputbridge/haptics/dualsense/trigger/left/vibration i i i i (deviceId, position, amplitude, frequency)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/vibration" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int position = argv[1]->i;
        int amplitude = argv[2]->i;
        int frequency = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            params["position"] = position;
            params["amplitude"] = amplitude;
            params["frequency"] = frequency;
            gamepad->SendDualSenseTrigger("left", "vibration", params);
        });
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/vibration" && std::strcmp(types, "iiii") == 0 && argc == 4) {
        int position = argv[1]->i;
        int amplitude = argv[2]->i;
        int frequency = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            params["position"] = position;
            params["amplitude"] = amplitude;
            params["frequency"] = frequency;
            gamepad->SendDualSenseTrigger("right", "vibration", params);
        });
    }
    // Example: /inputbridge/haptics/dualsense/trigger/left/off i (deviceId)
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/left/off" && std::strcmp(types, "i") == 0 && argc == 1) {
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            gamepad->SendDualSenseTrigger("left", "off", params);
        });
    }
    else if (path_sv == "/inputbridge/haptics/dualsense/trigger/right/off" && std::strcmp(types, "i") == 0 && argc == 1) {
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            std::map<std::string, int> params;
            gamepad->SendDualSenseTrigger("right", "off", params);
        });
    }
    // RPM LED meter — /inputbridge/wheel/led_rpm f  (value 0.0 – 1.0)
    // Sets the RPM LED bar on all connected RPM-capable steering wheels.
    else if (path_sv == "/inputbridge/wheel/led_rpm" && std::strcmp(types, "f") == 0 && argc == 1) {
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

    const auto last_slash = path_sv.rfind('/');
    if (last_slash == std::string_view::npos) return;
    const auto tail = path_sv.substr(last_slash + 1);
    if (tail.empty()) return;
    bool all_digits = true;
    for (char c : tail) { if (c < '0' || c > '9') { all_digits = false; break; } }
    if (!all_digits) return;

    int slot = 0;
    for (char c : tail) slot = slot * 10 + (c - '0');
    const std::string_view base = path_sv.substr(0, last_slash);

    // /inputbridge/haptics/rumble/N  iffi  (id, low_freq, high_freq, duration_ms)
    if (base == "/inputbridge/haptics/rumble" && std::strcmp(types, "iffi") == 0 && argc == 4) {
        const float low      = argv[1]->f;
        const float high     = argv[2]->f;
        const int   duration = argv[3]->i;
        DispatchHapticCommand<GamepadHaptics>([&](GamepadHaptics* gamepad) {
            gamepad->PlayRumble(slot, low, high, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/force/N  ifi  (id, strength, duration_ms)
    else if (base == "/inputbridge/haptics/force" && std::strcmp(types, "ifi") == 0 && argc == 3) {
        const float strength = argv[1]->f;
        const int   duration = argv[2]->i;
        DispatchHapticCommand<SteeringWheelHaptics>([&](SteeringWheelHaptics* wheel) {
            wheel->PlayConstant(slot, strength, (duration < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration);
        });
    }
    // /inputbridge/haptics/periodic/N  iififfii  (id, wave_type, strength, period, magnitude, offset, phase, duration_ms)
    else if (base == "/inputbridge/haptics/periodic" && std::strcmp(types, "iififfii") == 0 && argc == 8) {
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
    else if (base == "/inputbridge/haptics/periodic" && std::strcmp(types, "ififfii") == 0 && argc == 7) {
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
    else if (base == "/inputbridge/haptics/condition" && std::strcmp(types, "iiffffffi") == 0 && argc == 9) {
        const HapticConditionType ctype   = ConditionTypeFromIndex(argv[1]->i);
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
    // Note: /inputbridge/haptics/gain has no slot dimension — no subchannel variant is defined.
}