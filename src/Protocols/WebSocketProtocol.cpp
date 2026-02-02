#include "WebSocketProtocol.h"
#include "Devices/DeviceManager.h"
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
    try {
        json data = json::parse(message);

        SDL_JoystickID instance_id = data.at("device");
        std::string type = data.at("type");
        std::string effect = data.at("effect");
        json params = data.at("params");

        auto &deviceManager = DeviceManager::GetInstance();
        HapticDevice *haptic_device = deviceManager.GetHapticDevice(instance_id);

        if (!haptic_device) {
            return;
        }

        if (type == "gamepad" && effect == "rumble") {
            if (auto *gamepad_haptics = dynamic_cast<GamepadHaptics *>(haptic_device)) {
                float large_magnitude = params.at("large_magnitude");
                float small_magnitude = params.at("small_magnitude");
                uint32_t duration_ms = params.at("duration_ms");
                gamepad_haptics->Rumble(large_magnitude, small_magnitude, duration_ms);
            }
        } else if (type == "steering_wheel") {
            if (auto *wheel_haptics = dynamic_cast<SteeringWheelHaptics *>(haptic_device)) {
                if (effect == "constant") {
                    float strength = params.at("strength");
                    uint32_t duration_ms = params.at("duration_ms");
                    wheel_haptics->PlayConstant(strength, duration_ms);
                } else if (effect == "periodic") {
                    float strength = params.at("strength");
                    uint32_t period = params.at("period");
                    float magnitude = params.at("magnitude");
                    float offset = params.at("offset");
                    uint32_t phase = params.at("phase");
                    uint32_t duration_ms = params.at("duration_ms");
                    wheel_haptics->PlayPeriodic(strength, period, magnitude, offset, phase, duration_ms);
                } else if (effect == "condition") {
                    float right_sat = params.at("right_sat");
                    float left_sat = params.at("left_sat");
                    float right_coeff = params.at("right_coeff");
                    float left_coeff = params.at("left_coeff");
                    float deadband = params.at("deadband");
                    float center = params.at("center");
                    uint32_t duration_ms = params.at("duration_ms");
                    wheel_haptics->PlayCondition(right_sat, left_sat, right_coeff, left_coeff, deadband, center, duration_ms);
                }
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
