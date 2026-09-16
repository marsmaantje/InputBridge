#include "WebSocketProtocol.h"
#include "Mappers/OutputMapper.h"
#include "Devices/DeviceManager.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <map>

using json = nlohmann::json;

// Helper function from WebSocketServer.cpp
static std::string formatFloat(float val, int precision) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, val);
    std::string s(buf);
    std::replace(s.begin(), s.end(), ',', '.');
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.')
        s.pop_back();
    return s;
}

WebSocketProtocol::WebSocketProtocol(ProtocolVersion version) : m_version(version) {}

void WebSocketProtocol::setProtocolVersion(ProtocolVersion version) { m_version = version; }

WebSocketProtocol::ProtocolVersion WebSocketProtocol::getProtocolVersion() const { return m_version; }

std::string WebSocketProtocol::getProtocolName() const { return "WebSocket"; }

std::string WebSocketProtocol::format(const std::string &address, float value) {
    // A simple example format: "address:value"
    return address + ":" + formatFloat(value, 4);
}

std::string WebSocketProtocol::format(const std::string &address, int value) {
    // A simple example format: "address:value"
    return address + ":" + std::to_string(value);
}

std::string WebSocketProtocol::format(const std::string &address, const std::string &value) {
    // A simple example format: "address:value"
    return address + ":" + value;
}

std::string WebSocketProtocol::format_wheel(const std::map<std::string, float>& values) {
    std::string msg;
    msg.reserve(64);

    switch (m_version) {
    case ProtocolVersion::MarsmaantjeOld:
        if (values.count("wheel")) {
            msg += (char)0x01;
            msg += formatFloat(values.at("wheel"), 4);
            msg += ";";
        }
        if (values.count("brake")) {
            msg += 0x02;
            msg += formatFloat(values.at("brake"), 3);
            msg += ";";
        }
        if (values.count("throttle")) {
            msg += 0x03;
            msg += formatFloat(values.at("throttle"), 3);
            msg += ";";
        }
        break;
    case ProtocolVersion::MarsmaantjeNew:
        if (values.count("wheel")) {
            msg += "y";
            msg += formatFloat(values.at("wheel"), 8);
            msg += ";";
        }
        if (values.count("brake")) {
            msg += "b";
            msg += formatFloat(values.at("brake"), 5);
            msg += ";";
        }
        if (values.count("throttle")) {
            msg += "t";
            msg += formatFloat(values.at("throttle"), 5);
            msg += ";";
        }
        if (values.count("pitch")) {
            msg += "p";
            msg += formatFloat(values.at("pitch"), 8);
            msg += ";";
        }
        if (values.count("roll")) {
            msg += "r";
            msg += formatFloat(values.at("roll"), 8);
            msg += ";";
        }
        break;
    }

    return msg;
}

bool WebSocketProtocol::parse(const std::string &message) {
    bool handled = false;

    // try parse a single float
    try {
        // replace comma with dot for float parsing
        std::string msg = message;
        std::replace(msg.begin(), msg.end(), ',', '.');
        float value = -std::stof(msg);

        OutputMapper::GetInstance().QueueConstantForce(0, 0, value * 50, -1);
        handled = true;

    } catch (const std::exception &e) {
        // Not a valid float message, ignore
    }

    try {
        json data = json::parse(message);

        std::string type = data.at("type");
        std::string effect = data.at("effect");
        json params = data.at("params");

        if (type == "gamepad" && effect == "rumble") {
            float large_magnitude = params.at("large_magnitude");
            float small_magnitude = params.at("small_magnitude");
            int duration_ms = params.at("duration_ms");
            OutputMapper::GetInstance().QueueRumble(0, 0, large_magnitude, small_magnitude, duration_ms);
        } else if (type == "gamepad" && effect == "dualsense_trigger") {
            // DualSense adaptive trigger effect
            std::string trigger = params.value("trigger", "left");  // "left", "right", or "both"
            std::string effect_type = params.value("effect_type", "off");  // off, feedback, weapon, vibration, slope_feedback, multi_position_feedback, multi_position_vibration, bow, galloping, machine
            // slope_feedback reuses the generic position/strength/end_position/amplitude
            // fields as start_position/start_strength/end_position/end_strength - see
            // OutputMapper::TriggerDualSenseTrigger, which builds those named keys.

            if (effect_type == "multi_position_feedback") {
                // "strengths": [s0..s9], one value per trigger position (0-9). These
                // bypass QueueDualSenseTrigger entirely - see OutputMapper::
                // DualSenseArrayCommand for why.
                uint8_t strengths[10] = {0};
                if (params.contains("strengths") && params["strengths"].is_array()) {
                    const auto& arr = params["strengths"];
                    for (size_t i = 0; i < 10 && i < arr.size(); ++i)
                        strengths[i] = static_cast<uint8_t>(arr[i].get<int>());
                }
                OutputMapper::GetInstance().QueueDualSenseMultiPositionFeedback(0, trigger.c_str(), strengths);
            } else if (effect_type == "multi_position_vibration") {
                // "amplitudes": [a0..a9], one value per trigger position (0-9), plus
                // a single shared "frequency".
                uint8_t amplitudes[10] = {0};
                if (params.contains("amplitudes") && params["amplitudes"].is_array()) {
                    const auto& arr = params["amplitudes"];
                    for (size_t i = 0; i < 10 && i < arr.size(); ++i)
                        amplitudes[i] = static_cast<uint8_t>(arr[i].get<int>());
                }
                uint8_t frequency = static_cast<uint8_t>(params.value("frequency", 10));
                OutputMapper::GetInstance().QueueDualSenseMultiPositionVibration(0, trigger.c_str(), frequency, amplitudes);
            } else {
                int position = params.value("position", 0);
                int strength = params.value("strength", 5);
                int end_position = params.value("end_position", 9);
                int amplitude = params.value("amplitude", 5);
                int frequency = params.value("frequency", 10);
                int snap_force = params.value("snap_force", 5);
                int first_foot = params.value("first_foot", 2);
                int second_foot = params.value("second_foot", 7);
                int period = params.value("period", 10);
                int amplitude_a = params.value("amplitude_a", 4);
                int amplitude_b = params.value("amplitude_b", 4);

                OutputMapper::GetInstance().QueueDualSenseTrigger(0, trigger.c_str(), effect_type.c_str(),
                                                                  position, strength, end_position,
                                                                  amplitude, frequency, snap_force,
                                                                  first_foot, second_foot, period,
                                                                  amplitude_a, amplitude_b);
            }
        } else if (type == "gamepad" && effect == "xbox_trigger") {
            // Xbox impulse trigger effect
            int left_intensity = params.value("left_intensity", 0);
            int right_intensity = params.value("right_intensity", 0);
            int duration_ms = params.value("duration_ms", 0);
            OutputMapper::GetInstance().QueueXboxTrigger(0, left_intensity, right_intensity, duration_ms);
        } else if (type == "steering_wheel") {
            if (effect == "constant") {
                float strength = params.at("strength");
                int duration_ms = params.at("duration_ms");
                OutputMapper::GetInstance().QueueConstantForce(0, 0, strength, duration_ms);
            } else if (effect == "periodic") {
                float strength = params.at("strength");
                int period = params.at("period");
                float magnitude = params.at("magnitude");
                float offset = params.at("offset");
                int phase = params.at("phase");
                int duration_ms = params.at("duration_ms");
                // wave_type: 0=Sine (default), 1=Triangle, 2=SawtoothUp, 3=SawtoothDown
                HapticPeriodicType wave_type = PeriodicTypeFromIndex(params.value("wave_type", 0));
                OutputMapper::GetInstance().QueuePeriodic(0, 0, wave_type, strength, period, magnitude, offset, phase, duration_ms);
            } else if (effect == "condition") {
                int slot = params.value("slot", 0);
                // condition_type: 0=Spring, 1=Damper, 2=Inertia, 3=Friction
                HapticConditionType condition_type = ConditionTypeFromIndex(params.value("condition_type", 0));

                float right_sat = params.at("right_sat");
                float left_sat = params.at("left_sat");
                float right_coeff = params.at("right_coeff");
                float left_coeff = params.at("left_coeff");
                float deadband = params.at("deadband");
                float center = params.at("center");
                int duration_ms = params.at("duration_ms");
                // Note: Assumes OutputMapper::QueueCondition is updated to accept a slot.
                OutputMapper::GetInstance().QueueCondition(0, slot, condition_type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms);
            } else if (effect == "gain") {
                int value = params.at("value");
                OutputMapper::GetInstance().QueueSetGain(0, value);
            }
        }
        handled = true;
    } catch (const json::exception &e) {
        // Not a valid haptic message, ignore
    }

    return handled;
}

const char *WebSocketProtocol::GetVersionLabel(int index) {
    switch (static_cast<ProtocolVersion>(index)) {
    case ProtocolVersion::MarsmaantjeOld:
        return "Marsmaantje (Old)";
    case ProtocolVersion::MarsmaantjeNew:
        return "Marsmaantje (New)";
    }
    return "Unknown";
}

int WebSocketProtocol::GetVersionCount() { return 2; }