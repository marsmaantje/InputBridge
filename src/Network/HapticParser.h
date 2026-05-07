#pragma once

#include <string_view>
#include <string>
#include "Haptics/HapticDevice.h"

class OutputMapper;

/**
 * @brief Result of HapticParser::AutoDetect().
 *
 * When a client sends a message without an explicit "type" field (or with
 * "type": "auto"), AutoDetect sniffs the payload fields and fills this struct
 * so Parse() can dispatch the right effect without any help from the caller.
 */
struct DetectedEffect {
    enum class Kind {
        Unknown,
        Rumble,
        Constant,
        Periodic,
        Condition,
        DualSenseTrigger,
    };

    Kind    kind    = Kind::Unknown;
    int     device  = 0;
    int     slot    = 0;
    int     duration_ms = 0;

    // Rumble
    float   low     = 0.0f;
    float   high    = 0.0f;

    // Constant
    float   strength = 0.0f;

    // Periodic
    int     period    = 0;
    float   magnitude = 0.0f;
    float   offset    = 0.0f;
    int     phase     = 0;
    HapticPeriodicType wave_type = HapticPeriodicType::Sine;  // 0=Sine, 1=Triangle, 2=SawtoothUp, 3=SawtoothDown

    // Condition
    HapticConditionType condition_type = HapticConditionType::Spring;  // 0=Spring, 1=Damper, 2=Inertia, 3=Friction
    float    right_sat       = 0.0f;
    float    left_sat        = 0.0f;
    float    right_coeff     = 0.0f;
    float    left_coeff      = 0.0f;
    float    deadband        = 0.0f;
    float    center          = 0.0f;

    // DualSense trigger
    std::string trigger;        // "left", "right", or "both"
    std::string effect_type;    // e.g. "feedback", "weapon", "vibration"
};

class HapticParser {
public:
    /**
     * @brief Parse and dispatch a JSON haptic command.
     *
     * Accepted "type" values: "haptic", "gamepad", "steering_wheel",
     * "flight_stick", and "auto".  When "type" is "auto" or absent the
     * message is forwarded to AutoDetect() first.
     */
    static void Parse(std::string_view message, OutputMapper* mapper);

    /**
     * @brief Infer the haptic effect described by a JSON payload.
     *
     * Inspects which fields are present in the JSON object (or its nested
     * "params" / "data" sub-object) and returns a fully populated
     * DetectedEffect.  kind == DetectedEffect::Kind::Unknown means no
     * recognisable haptic fields were found.
     *
     * Field priority (checked in order so the most-specific wins):
     *   1. DualSense trigger  — "trigger" + "effect_type"
     *   2. Condition          — "right_sat" / "left_sat" / "condition_type"
     *   3. Periodic           — "period" (or "magnitude" + "offset" together)
     *   4. Constant           — "strength" alone
     *   5. Rumble             — "low"/"high" or "large_magnitude"/"small_magnitude"
     *
     * If the message also carries an explicit "effect" field whose value
     * matches a known effect name, that takes precedence over field sniffing.
     */
    static DetectedEffect AutoDetect(std::string_view message);
};