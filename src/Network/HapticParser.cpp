#include "HapticParser.h"
#include "../Mappers/OutputMapper.h"
#include <nlohmann/json.hpp>
#include <SDL3/SDL_haptic.h>

namespace {
    // JSON keys for haptic commands
    const char* const kType = "type";
    const char* const kEffect = "effect";
    const char* const kDevice = "device";
    const char* const kParams = "params";
    const char* const kData = "data";

    // Type values
    const char* const kTypeHaptic = "haptic";
    const char* const kTypeGamepad = "gamepad";
    const char* const kTypeSteeringWheel = "steering_wheel";

    // Effect values
    const char* const kEffectRumble = "rumble";
    const char* const kEffectConstant = "constant";
    const char* const kEffectPeriodic = "periodic";
    const char* const kEffectCondition = "condition";

    // Rumble params
    const char* const kRumbleLow = "low";
    const char* const kRumbleHigh = "high";
    const char* const kRumbleLargeMag = "large_magnitude";
    const char* const kRumbleSmallMag = "small_magnitude";

    // Constant params
    const char* const kConstantStrength = "strength";

    // Periodic params
    const char* const kPeriodicPeriod = "period";
    const char* const kPeriodicMagnitude = "magnitude";
    const char* const kPeriodicOffset = "offset";
    const char* const kPeriodicPhase = "phase";

    // Condition params
    const char* const kConditionSlot = "slot";
    const char* const kConditionType = "condition_type";

    // Shared slot key used by all slotted effect types
    const char* const kSlot = "slot";
    const char* const kConditionRightSat = "right_sat";
    const char* const kConditionLeftSat = "left_sat";
    const char* const kConditionRightCoeff = "right_coeff";
    const char* const kConditionLeftCoeff = "left_coeff";
    const char* const kConditionDeadband = "deadband";
    const char* const kConditionCenter = "center";

    // Common params
    const char* const kDuration = "duration";
    const char* const kDurationMs = "duration_ms";

    // Helper to get the nested "params" or "data" object.
    // Returns a reference to avoid copying. Returns an empty object if neither key exists.
    const nlohmann::json& get_params_obj(const nlohmann::json& json)
    {
        if (json.contains(kParams)) {
            return json.at(kParams);
        }
        if (json.contains(kData)) {
            return json.at(kData);
        }
        // Return a static empty object to avoid creating it on every call.
        static const nlohmann::json empty_obj = nlohmann::json::object();
        return empty_obj;
    }

    // Helper to get duration, preferring "duration_ms" over "duration".
    int get_duration(const nlohmann::json& data)
    {
        if (data.contains(kDurationMs)) return data.value(kDurationMs, 0);
        return data.value(kDuration, 0);
    }
}

void HapticParser::Parse(std::string_view message, OutputMapper* mapper) {
    if (!mapper) {
        return;
    }

    try {
        auto json = nlohmann::json::parse(message);
        std::string type = json.value(kType, "");
        if (type == kTypeHaptic || type == kTypeGamepad || type == kTypeSteeringWheel) {
            std::string effect = json.value(kEffect, "");
            int device = json.value(kDevice, 0);
            const auto& data = get_params_obj(json);

            if (effect == kEffectRumble) {
                float low = data.value(kRumbleLow, 0.0f);
                if (data.contains(kRumbleLargeMag)) low = data.value(kRumbleLargeMag, 0.0f);

                float high = data.value(kRumbleHigh, 0.0f);
                if (data.contains(kRumbleSmallMag)) high = data.value(kRumbleSmallMag, 0.0f);

                int duration = get_duration(data);
                int slot = data.value(kSlot, 0);

                mapper->QueueRumble(device, slot, low, high, duration);
            } else if (effect == kEffectConstant) {
                float strength = data.value(kConstantStrength, 0.0f);
                int duration = get_duration(data);
                int slot = data.value(kSlot, 0);

                mapper->QueueConstantForce(device, slot, strength, duration);
            } else if (effect == kEffectPeriodic) {
                int duration = get_duration(data);
                int slot = data.value(kSlot, 0);

                mapper->QueuePeriodic(device, slot, data.value(kConstantStrength, 0.0f), data.value(kPeriodicPeriod, 0), data.value(kPeriodicMagnitude, 0.0f), data.value(kPeriodicOffset, 0.0f), data.value(kPeriodicPhase, 0), duration);
            } else if (effect == kEffectCondition) {
                int duration = get_duration(data);

                int slot = data.value(kConditionSlot, 0);
                uint16_t condition_type = data.value(kConditionType, (uint16_t)SDL_HAPTIC_SPRING);

                mapper->QueueCondition(device, slot, condition_type, data.value(kConditionRightSat, 0.0f), data.value(kConditionLeftSat, 0.0f), data.value(kConditionRightCoeff, 0.0f), data.value(kConditionLeftCoeff, 0.0f), data.value(kConditionDeadband, 0.0f), data.value(kConditionCenter, 0.0f), duration);
            }
        }
    } catch (...) {
        // Ignore JSON parsing errors for messages not intended for this parser
    }
}
