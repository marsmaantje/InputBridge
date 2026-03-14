#include "Protocols/MarsmaantjeNewProtocol.h"
#include "Mappers/OutputMapper.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <map>
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

std::string MarsmaantjeNewProtocol::format_wheel(const std::map<std::string, float>& values) {
    std::string msg;
    msg.reserve(64);
    if (values.count("wheel") && values.at("wheel") != 0.0f) {
        msg += "y";
        msg += formatFloat(values.at("wheel"), 8);
        msg += ";";
    }
    if (values.count("brake") && values.at("brake") != 0.0f) {
        msg += "b";
        msg += formatFloat(values.at("brake"), 5);
        msg += ";";
    }
    if (values.count("throttle") && values.at("throttle") != 0.0f) {
        msg += "t";
        msg += formatFloat(values.at("throttle"), 5);
        msg += ";";
    }
    if (values.count("pitch") && values.at("pitch") != 0.0f) {
        msg += "p";
        msg += formatFloat(values.at("pitch"), 8);
        msg += ";";
    }
    if (values.count("roll") && values.at("roll") != 0.0f) {
        msg += "r";
        msg += formatFloat(values.at("roll"), 8);
        msg += ";";
    }
    return msg;
}

void MarsmaantjeNewProtocol::parse(const std::string& message) {
    try {
        std::string msg = message;
        std::replace(msg.begin(), msg.end(), ',', '.');
        float value = -std::stof(msg);
        OutputMapper::GetInstance().QueueConstantForce(0, 0, value * 50, -1);
    } catch (...) {}

    try {
        json data = json::parse(message);
        std::string type = data.value("type", "");
        std::string effect = data.value("effect", "");

        // Additional JSON parsing logic can be added here if needed
    } catch (...) {}
}
