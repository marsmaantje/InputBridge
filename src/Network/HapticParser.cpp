#include "HapticParser.h"
#include "../Mappers/OutputMapper.h"
#include <nlohmann/json.hpp>
#include <SDL3/SDL_haptic.h>

void HapticParser::Parse(std::string_view message, OutputMapper* mapper) {
    if (!mapper) {
        return;
    }

    try {
        auto json = nlohmann::json::parse(message);
        std::string type = json.value("type", "");
        if (type == "haptic" || type == "gamepad" || type == "steering_wheel") {
            std::string effect = json.value("effect", "");
            int device = json.value("device", 0);

            nlohmann::json data;
            if (json.contains("params")) {
                data = json["params"];
            } else if (json.contains("data")) {
                data = json["data"];
            } else {
                data = nlohmann::json::object();
            }

            if (effect == "rumble") {
                float low = data.value("low", 0.0f);
                if (data.contains("large_magnitude")) low = data.value("large_magnitude", 0.0f);

                float high = data.value("high", 0.0f);
                if (data.contains("small_magnitude")) high = data.value("small_magnitude", 0.0f);

                int duration = data.value("duration", 0);
                if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                mapper->QueueRumble(device, low, high, duration);
            } else if (effect == "constant") {
                float strength = data.value("strength", 0.0f);
                int duration = data.value("duration", 0);
                if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                mapper->QueueConstantForce(device, strength, duration);
            } else if (effect == "periodic") {
                int duration = data.value("duration", 0);
                if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                mapper->QueuePeriodic(device, data.value("strength", 0.0f), data.value("period", 0), data.value("magnitude", 0.0f), data.value("offset", 0.0f), data.value("phase", 0), duration);
            } else if (effect == "condition") {
                int duration = data.value("duration", 0);
                if (data.contains("duration_ms")) duration = data.value("duration_ms", 0);

                int slot = data.value("slot", 0);
                uint16_t condition_type = data.value("condition_type", (uint16_t)SDL_HAPTIC_SPRING);

                mapper->QueueCondition(device, slot, condition_type, data.value("right_sat", 0.0f), data.value("left_sat", 0.0f), data.value("right_coeff", 0.0f), data.value("left_coeff", 0.0f), data.value("deadband", 0.0f), data.value("center", 0.0f), duration);
            }
        }
    } catch (...) {
        // Ignore JSON parsing errors for messages not intended for this parser
    }
}