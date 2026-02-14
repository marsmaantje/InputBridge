#include "Protocols/MarsmaantjeNewProtocol.h"
#include "Mappers/OutputMapper.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

std::string MarsmaantjeNewProtocol::format(const std::string &address, float value) {
    return address + ":" + formatFloat(value, 4);
}

std::string MarsmaantjeNewProtocol::format(const std::string &address, int value) {
    return address + ":" + std::to_string(value);
}

std::string MarsmaantjeNewProtocol::format(const std::string &address, const std::string &value) {
    return address + ":" + value;
}

std::string MarsmaantjeNewProtocol::format_wheel(float wheel, float brake, float throttle, float pitch, float roll) {
    std::string msg;
    msg.reserve(64);
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
    return msg;
}

void MarsmaantjeNewProtocol::parse(const std::string& message) {
    try {
        std::string msg = message;
        std::replace(msg.begin(), msg.end(), ',', '.');
        float value = -std::stof(msg);
        OutputMapper::GetInstance().QueueConstantForce(0, value * 50, -1);
    } catch (...) {}

    try {
        json data = json::parse(message);
        std::string type = data.value("type", "");
        std::string effect = data.value("effect", "");

        // Additional JSON parsing logic can be added here if needed
    } catch (...) {}
}
