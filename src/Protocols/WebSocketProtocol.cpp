#include "WebSocketProtocol.h"
#include <algorithm>
#include <cstdio>
#include <string>

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

WebSocketProtocol::WebSocketProtocol(WheelProtocolVersion version)
    : m_version(version) {}

void WebSocketProtocol::setWheelProtocolVersion(WheelProtocolVersion version) {
    m_version = version;
}

WebSocketProtocol::WheelProtocolVersion
WebSocketProtocol::getWheelProtocolVersion() const {
    return m_version;
}

std::string WebSocketProtocol::getProtocolName() const { return "WebSocket"; }

std::string WebSocketProtocol::format(const std::string &address, float value) {
    // A simple example format: "address:value"
    return address + ":" + formatFloat(value, 4);
}

std::string WebSocketProtocol::format(const std::string &address, int value) {
    // A simple example format: "address:value"
    return address + ":" + std::to_string(value);
}

std::string WebSocketProtocol::format(const std::string &address,
                                      const std::string &value) {
    // A simple example format: "address:value"
    return address + ":" + value;
}

std::string WebSocketProtocol::format_wheel(float wheel, float brake,
                                            float throttle) {
    std::string msg;
    msg.reserve(64);

    switch (m_version) {
    case WheelProtocolVersion::MarsmaantjeOld:
        msg += (char)1;
        msg += formatFloat(wheel, 4);
        msg += ";";
        msg += (char)2;
        msg += formatFloat(brake, 3);
        msg += ";";
        msg += (char)3;
        msg += formatFloat(throttle, 3);
        msg += ";";
        break;
    case WheelProtocolVersion::MarsmaantjeNew:
        msg += "a";
        msg += formatFloat(wheel, 4);
        msg += ";";
        msg += "b";
        msg += formatFloat(brake, 3);
        msg += ";";
        msg += "c";
        msg += formatFloat(throttle, 3);
        msg += ";";
        break;
    }

    return msg;
}

const char *WebSocketProtocol::GetVersionLabel(int index) {
    switch (static_cast<WheelProtocolVersion>(index)) {
    case WheelProtocolVersion::MarsmaantjeOld:
        return "Marsmaantje (Old)";
    case WheelProtocolVersion::MarsmaantjeNew:
        return "Marsmaantje (New)";
    }
    return "Unknown";
}

int WebSocketProtocol::GetVersionCount() { return 2; }
