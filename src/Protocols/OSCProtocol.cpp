#include "OSCProtocol.h"
#include <cstring>
#include <cstdint>

static bool IsBigEndian() {
    union {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};
    return bint.c[0] == 1;
}

static uint32_t Swap32(uint32_t val) {
    return ((val >> 24) & 0xff) | ((val << 8) & 0xff0000) |
           ((val >> 8) & 0xff00) | ((val << 24) & 0xff000000);
}

static void AppendString(std::string& buf, const std::string& str) {
    buf.append(str);
    buf.push_back('\0');
    while (buf.size() % 4 != 0) {
        buf.push_back('\0');
    }
}

static void AppendFloat(std::string& buf, float f) {
    uint32_t val;
    std::memcpy(&val, &f, 4);
    if (!IsBigEndian()) {
        val = Swap32(val);
    }
    buf.append(reinterpret_cast<const char*>(&val), 4);
}

static void AppendInt(std::string& buf, int i) {
    uint32_t val;
    std::memcpy(&val, &i, 4);
    if (!IsBigEndian()) {
        val = Swap32(val);
    }
    buf.append(reinterpret_cast<const char*>(&val), 4);
}

std::string OSCProtocol::getProtocolName() const {
    return "OSC";
}

std::string OSCProtocol::format(const std::string& address, float value) {
    std::string msg;
    AppendString(msg, address);
    AppendString(msg, ",f");
    AppendFloat(msg, value);
    return msg;
}

std::string OSCProtocol::format(const std::string& address, int value) {
    std::string msg;
    AppendString(msg, address);
    AppendString(msg, ",i");
    AppendInt(msg, value);
    return msg;
}

std::string OSCProtocol::format(const std::string& address, const std::string& value) {
    std::string msg;
    AppendString(msg, address);
    AppendString(msg, ",s");
    AppendString(msg, value);
    return msg;
}

std::string OSCProtocol::format_wheel(float wheel, float brake, float throttle) {
    // Not applicable for generic OSC, but could be implemented as a bundle
    return "";
}
