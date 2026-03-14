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
    const char* const kTypeHaptic        = "haptic";
    const char* const kTypeGamepad       = "gamepad";
    const char* const kTypeSteeringWheel = "steering_wheel";
    const char* const kTypeFlightStick   = "flight_stick";
    const char* const kTypeAuto          = "auto";

    // Effect values
    const char* const kEffectRumble    = "rumble";
    const char* const kEffectConstant  = "constant";
    const char* const kEffectPeriodic  = "periodic";
    const char* const kEffectCondition = "condition";

    // Rumble params
    const char* const kRumbleLow      = "low";
    const char* const kRumbleHigh     = "high";
    const char* const kRumbleLargeMag = "large_magnitude";
    const char* const kRumbleSmallMag = "small_magnitude";

    // Constant params
    const char* const kConstantStrength = "strength";

    // Periodic params
    const char* const kPeriodicPeriod    = "period";
    const char* const kPeriodicMagnitude = "magnitude";
    const char* const kPeriodicOffset    = "offset";
    const char* const kPeriodicPhase     = "phase";

    // Condition params
    const char* const kConditionSlot      = "slot";
    const char* const kConditionType      = "condition_type";
    const char* const kConditionRightSat  = "right_sat";
    const char* const kConditionLeftSat   = "left_sat";
    const char* const kConditionRightCoeff = "right_coeff";
    const char* const kConditionLeftCoeff  = "left_coeff";
    const char* const kConditionDeadband  = "deadband";
    const char* const kConditionCenter    = "center";

    // DualSense params
    const char* const kTrigger     = "trigger";
    const char* const kEffectType  = "effect_type";

    // Shared slot key used by all slotted effect types
    const char* const kSlot = "slot";

    // Common params
    const char* const kDuration   = "duration";
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

    // Returns true if the type string is one of the accepted device type strings.
    bool is_known_type(const std::string& type) {
        return type == kTypeHaptic
            || type == kTypeGamepad
            || type == kTypeSteeringWheel
            || type == kTypeFlightStick;
    }

    // Core dispatch logic: dispatch a parsed effect from a known JSON payload.
    // Called by both the normal Parse path and the AutoDetect path.
    void dispatch(const nlohmann::json& json, const nlohmann::json& data,
                  const std::string& effect, int device, OutputMapper* mapper)
    {
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
            int   duration = get_duration(data);
            int   slot     = data.value(kSlot, 0);

            mapper->QueueConstantForce(device, slot, strength, duration);

        } else if (effect == kEffectPeriodic) {
            int   duration = get_duration(data);
            int   slot     = data.value(kSlot, 0);

            mapper->QueuePeriodic(device, slot,
                data.value(kConstantStrength, 0.0f),
                data.value(kPeriodicPeriod,   0),
                data.value(kPeriodicMagnitude,0.0f),
                data.value(kPeriodicOffset,   0.0f),
                data.value(kPeriodicPhase,    0),
                duration);

        } else if (effect == kEffectCondition) {
            int      duration       = get_duration(data);
            int      slot           = data.value(kConditionSlot, 0);
            uint16_t condition_type = data.value(kConditionType, (uint16_t)SDL_HAPTIC_SPRING);

            mapper->QueueCondition(device, slot, condition_type,
                data.value(kConditionRightSat,   0.0f),
                data.value(kConditionLeftSat,    0.0f),
                data.value(kConditionRightCoeff, 0.0f),
                data.value(kConditionLeftCoeff,  0.0f),
                data.value(kConditionDeadband,   0.0f),
                data.value(kConditionCenter,     0.0f),
                duration);
        }
    }

    // Sniff a flat JSON object (the top-level payload *or* its params sub-object)
    // and return the inferred DetectedEffect::Kind plus populate out.
    // Returns Kind::Unknown if nothing recognisable is found.
    DetectedEffect::Kind sniff(const nlohmann::json& flat, DetectedEffect& out)
    {
        // 1. DualSense trigger: needs both "trigger" and "effect_type".
        if (flat.contains(kTrigger) && flat.contains(kEffectType)) {
            out.trigger     = flat.value(kTrigger,    std::string{});
            out.effect_type = flat.value(kEffectType, std::string{});
            return DetectedEffect::Kind::DualSenseTrigger;
        }

        // 2. Condition: distinguishing fields are the sat/coeff pair or condition_type.
        if (flat.contains(kConditionRightSat) || flat.contains(kConditionLeftSat)
                || flat.contains(kConditionType)) {
            out.condition_type = flat.value(kConditionType, (uint16_t)SDL_HAPTIC_SPRING);
            out.right_sat      = flat.value(kConditionRightSat,   0.0f);
            out.left_sat       = flat.value(kConditionLeftSat,    0.0f);
            out.right_coeff    = flat.value(kConditionRightCoeff, 0.0f);
            out.left_coeff     = flat.value(kConditionLeftCoeff,  0.0f);
            out.deadband       = flat.value(kConditionDeadband,   0.0f);
            out.center         = flat.value(kConditionCenter,     0.0f);
            out.slot           = flat.value(kConditionSlot, flat.value(kSlot, 0));
            out.duration_ms    = get_duration(flat);
            return DetectedEffect::Kind::Condition;
        }

        // 3. Periodic: "period" is the clearest signal; also accept "magnitude"+"offset"
        //    together (to distinguish from constant which only has "strength").
        if (flat.contains(kPeriodicPeriod)
                || (flat.contains(kPeriodicMagnitude) && flat.contains(kPeriodicOffset))) {
            out.strength    = flat.value(kConstantStrength, 0.0f);
            out.period      = flat.value(kPeriodicPeriod,    0);
            out.magnitude   = flat.value(kPeriodicMagnitude, 0.0f);
            out.offset      = flat.value(kPeriodicOffset,    0.0f);
            out.phase       = flat.value(kPeriodicPhase,     0);
            out.slot        = flat.value(kSlot, 0);
            out.duration_ms = get_duration(flat);
            return DetectedEffect::Kind::Periodic;
        }

        // 4. Constant: "strength" alone (no periodic sub-fields).
        if (flat.contains(kConstantStrength)) {
            out.strength    = flat.value(kConstantStrength, 0.0f);
            out.slot        = flat.value(kSlot, 0);
            out.duration_ms = get_duration(flat);
            return DetectedEffect::Kind::Constant;
        }

        // 5. Rumble: "low"/"high" aliases or "large_magnitude"/"small_magnitude".
        if (flat.contains(kRumbleLow) || flat.contains(kRumbleHigh)
                || flat.contains(kRumbleLargeMag) || flat.contains(kRumbleSmallMag)) {
            out.low  = flat.contains(kRumbleLargeMag) ? flat.value(kRumbleLargeMag, 0.0f)
                                                       : flat.value(kRumbleLow, 0.0f);
            out.high = flat.contains(kRumbleSmallMag) ? flat.value(kRumbleSmallMag, 0.0f)
                                                       : flat.value(kRumbleHigh, 0.0f);
            out.slot        = flat.value(kSlot, 0);
            out.duration_ms = get_duration(flat);
            return DetectedEffect::Kind::Rumble;
        }

        return DetectedEffect::Kind::Unknown;
    }

    // Map a DetectedEffect::Kind back to the string expected by dispatch().
    std::string kind_to_effect_string(DetectedEffect::Kind kind) {
        switch (kind) {
            case DetectedEffect::Kind::Rumble:    return kEffectRumble;
            case DetectedEffect::Kind::Constant:  return kEffectConstant;
            case DetectedEffect::Kind::Periodic:  return kEffectPeriodic;
            case DetectedEffect::Kind::Condition: return kEffectCondition;
            default:                              return "";
        }
    }

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// HapticParser::AutoDetect
// ─────────────────────────────────────────────────────────────────────────────

DetectedEffect HapticParser::AutoDetect(std::string_view message) {
    DetectedEffect out;

    try {
        auto json = nlohmann::json::parse(message);
        if (!json.is_object()) return out;

        out.device = json.value(kDevice, 0);

        // If an explicit "effect" key is present, use it as a strong hint
        // before falling back to field sniffing.
        std::string explicit_effect = json.value(kEffect, "");

        // Try the top-level object first, then the params/data sub-object.
        const auto& data = get_params_obj(json);

        // If the effect hint is already a known string, resolve it directly
        // so we don't accidentally mis-classify (e.g. "condition" with "strength").
        if (!explicit_effect.empty()) {
            if      (explicit_effect == kEffectRumble)    out.kind = DetectedEffect::Kind::Rumble;
            else if (explicit_effect == kEffectConstant)  out.kind = DetectedEffect::Kind::Constant;
            else if (explicit_effect == kEffectPeriodic)  out.kind = DetectedEffect::Kind::Periodic;
            else if (explicit_effect == kEffectCondition) out.kind = DetectedEffect::Kind::Condition;
        }

        if (out.kind == DetectedEffect::Kind::Unknown) {
            // Sniff the params sub-object first; fall back to the top-level.
            out.kind = sniff(data, out);
            if (out.kind == DetectedEffect::Kind::Unknown)
                out.kind = sniff(json, out);
        } else {
            // Kind was resolved from the "effect" string — still populate params.
            sniff(data.empty() ? json : data, out);
        }

        // For kinds that sniff() doesn't populate from the explicit effect hint path.
        if (out.slot == 0)        out.slot        = data.value(kSlot, json.value(kSlot, 0));
        if (out.duration_ms == 0) out.duration_ms = get_duration(data.empty() ? json : data);

    } catch (...) {
        // Malformed JSON — return Kind::Unknown.
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// HapticParser::Parse
// ─────────────────────────────────────────────────────────────────────────────

void HapticParser::Parse(std::string_view message, OutputMapper* mapper) {
    if (!mapper) {
        return;
    }

    try {
        auto json = nlohmann::json::parse(message);
        std::string type = json.value(kType, "");

        // "auto" (or absent type) → sniff the fields and dispatch accordingly.
        if (type == kTypeAuto || type.empty()) {
            DetectedEffect det = AutoDetect(message);
            if (det.kind == DetectedEffect::Kind::Unknown) return;

            const auto& data = get_params_obj(json);

            // DualSense triggers are not handled through the standard dispatch()
            // helper since they use QueueDualSenseTrigger, not the generic effects.
            // For now we fall through to the standard JSON path when a DualSense
            // trigger is auto-detected (the caller must still set "type" explicitly
            // for DualSense — auto-detect only covers the four SDL effect classes).
            if (det.kind != DetectedEffect::Kind::DualSenseTrigger) {
                dispatch(json, data.empty() ? json : data,
                         kind_to_effect_string(det.kind), det.device, mapper);
            }
            return;
        }

        // Named device type → standard parse path.
        if (is_known_type(type)) {
            std::string effect = json.value(kEffect, "");
            int device = json.value(kDevice, 0);
            const auto& data = get_params_obj(json);
            dispatch(json, data, effect, device, mapper);
        }

    } catch (...) {
        // Ignore JSON parsing errors for messages not intended for this parser
    }
}
