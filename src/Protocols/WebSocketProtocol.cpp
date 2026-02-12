#include "WebSocketProtocol.h"
#include "Mappers/OutputMapper.h"
#include <algorithm>
#include <cstdio>
#include <string>

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

std::string WebSocketProtocol::format_wheel(float wheel, float brake, float throttle, float pitch, float roll) {
    std::string msg;
    msg.reserve(64);

    switch (m_version) {
    case ProtocolVersion::MarsmaantjeOld:
        msg += 0x01;
        msg += formatFloat(wheel, 4);
        msg += ";";
        msg += 0x02;
        msg += formatFloat(brake, 3);
        msg += ";";
        msg += 0x03;
        msg += formatFloat(throttle, 3);
        msg += ";";
        break;
    case ProtocolVersion::MarsmaantjeNew:
        msg += "y";
        msg += formatFloat(wheel, 8);
        msg += ";";
        msg += "b";
        msg += formatFloat(brake, 5);
        msg += ";";
        msg += "t";
        msg += formatFloat(throttle, 5);
        msg += ";";
        msg += "p";
        msg += formatFloat(pitch, 8);
        msg += ";";
        msg += "r";
        msg += formatFloat(roll, 8);
        msg += ";";
        break;
    }

    return msg;
}

void WebSocketProtocol::parse(const std::string &message) {
    // try parse a single float
    try {
        // replace comma with dot for float parsing
        std::string msg = message;
        std::replace(msg.begin(), msg.end(), ',', '.');
        float value = -std::stof(msg);

        OutputMapper::GetInstance().QueueConstantForce(0, value * 50, -1);
        
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
            OutputMapper::GetInstance().QueueRumble(0, large_magnitude, small_magnitude, duration_ms);
        } else if (type == "gamepad" && effect == "dualsense_trigger") {
            // DualSense adaptive trigger effect
            std::string trigger = params.value("trigger", "left");  // "left", "right", or "both"
            std::string effect_type = params.value("effect_type", "off");  // off, feedback, weapon, vibration, bow, galloping, machine
            
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
        } else if (type == "steering_wheel") {
            if (effect == "constant") {
                float strength = params.at("strength");
                int duration_ms = params.at("duration_ms");
                OutputMapper::GetInstance().QueueConstantForce(0, strength, duration_ms);
            } else if (effect == "periodic") {
                float strength = params.at("strength");
                int period = params.at("period");
                float magnitude = params.at("magnitude");
                float offset = params.at("offset");
                int phase = params.at("phase");
                int duration_int = params.at("duration_ms");
                OutputMapper::GetInstance().QueuePeriodic(0, strength, period, magnitude, offset, phase, duration_int);
            } else if (effect == "condition") {
                float right_sat = params.at("right_sat");
                float left_sat = params.at("left_sat");
                float right_coeff = params.at("right_coeff");
                float left_coeff = params.at("left_coeff");
                float deadband = params.at("deadband");
                float center = params.at("center");
                int duration_int = params.at("duration_ms");
                OutputMapper::GetInstance().QueueCondition(0, right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_int);
            } else if (effect == "gain") {
                int value = params.at("value");
                OutputMapper::GetInstance().QueueSetGain(0, value);
            }
        }
    } catch (const json::exception &e) {
        // Not a valid haptic message, ignore
    }
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
