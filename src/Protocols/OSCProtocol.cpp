#include "OSCProtocol.h"
#include "Devices/DeviceManager.h"
#include "Haptics/GamepadHaptics.h"
#include "Haptics/SteeringWheelHaptics.h"
#include <cstdint>
#include <cstring>

static bool IsBigEndian() {
    union {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};
    return bint.c[0] == 1;
}

static uint32_t Swap32(uint32_t val) { return ((val >> 24) & 0xff) | ((val << 8) & 0xff0000) | ((val >> 8) & 0xff00) | ((val << 24) & 0xff000000); }

static void AppendString(std::string &buf, const std::string &str) {
    buf.append(str);
    buf.push_back('\0');
    while (buf.size() % 4 != 0) {
        buf.push_back('\0');
    }
}

static void AppendFloat(std::string &buf, float f) {
    uint32_t val;
    std::memcpy(&val, &f, 4);
    if (!IsBigEndian()) {
        val = Swap32(val);
    }
    buf.append(reinterpret_cast<const char *>(&val), 4);
}

static void AppendInt(std::string &buf, int i) {
    uint32_t val;
    std::memcpy(&val, &i, 4);
    if (!IsBigEndian()) {
        val = Swap32(val);
    }
    buf.append(reinterpret_cast<const char *>(&val), 4);
}

static std::string ReadString(const std::string &buf, size_t &pos) {
    size_t end = buf.find('\0', pos);
    if (end == std::string::npos) return "";
    std::string str = buf.substr(pos, end - pos);
    pos = end + 1;
    while (pos % 4 != 0 && pos < buf.size()) {
        pos++;
    }
    return str;
}

static int ReadInt(const std::string &buf, size_t &pos) {
    if (pos + 4 > buf.size()) return 0;
    uint32_t val;
    std::memcpy(&val, &buf[pos], 4);
    if (!IsBigEndian()) {
        val = Swap32(val);
    }
    pos += 4;
    return static_cast<int>(val);
}

static float ReadFloat(const std::string &buf, size_t &pos) {
    if (pos + 4 > buf.size()) return 0.0f;
    uint32_t val;
    std::memcpy(&val, &buf[pos], 4);
    if (!IsBigEndian()) {
        val = Swap32(val);
    }
    float f;
    std::memcpy(&f, &val, 4);
    pos += 4;
    return f;
}

std::string OSCProtocol::getProtocolName() const { return "OSC"; }

std::string OSCProtocol::format(const std::string &address, float value) {
    std::string msg;
    AppendString(msg, address);
    AppendString(msg, ",f");
    AppendFloat(msg, value);
    return msg;
}

std::string OSCProtocol::format(const std::string &address, int value) {
    std::string msg;
    AppendString(msg, address);
    AppendString(msg, ",i");
    AppendInt(msg, value);
    return msg;
}

std::string OSCProtocol::format(const std::string &address, const std::string &value) {
    std::string msg;
    AppendString(msg, address);
    AppendString(msg, ",s");
    AppendString(msg, value);
    return msg;
}

std::string OSCProtocol::format_wheel(float wheel, float brake, float throttle, float pitch, float roll) {
    // Not applicable for generic OSC, but could be implemented as a bundle
    return "";
}

void OSCProtocol::parse(const std::string& message) {
    size_t pos = 0;
    if (message.size() % 4 != 0) return;

    std::string address = ReadString(message, pos);
    if (address.empty() || address[0] != '/') return;

    std::string typeTags = ReadString(message, pos);
    if (typeTags.empty() || typeTags[0] != ',') return;

    // Helper to get next arg based on type tag
    auto getNextArg = [&](char type) -> float {
        if (type == 'f') return ReadFloat(message, pos);
        if (type == 'i') return static_cast<float>(ReadInt(message, pos));
        return 0.0f;
    };

    // First argument is always device ID (int)
    size_t arg_idx = 1; // skip ','
    int device_id = 0;
    
    if (arg_idx < typeTags.size() && typeTags[arg_idx] == 'i') {
        device_id = ReadInt(message, pos);
        arg_idx++;
    } else {
        return; 
    }

    auto& deviceManager = DeviceManager::GetInstance();
    HapticDevice* haptic_device = deviceManager.GetHapticDevice(device_id);
    if (!haptic_device) return;

    if (address == "/haptic/gamepad/rumble") {
        float large = 0.0f, small = 0.0f;
        int duration = 0;
        
        if (arg_idx < typeTags.size()) large = getNextArg(typeTags[arg_idx++]);
        if (arg_idx < typeTags.size()) small = getNextArg(typeTags[arg_idx++]);
        if (arg_idx < typeTags.size()) duration = (int)getNextArg(typeTags[arg_idx++]);

        if (auto* gamepad = dynamic_cast<GamepadHaptics*>(haptic_device)) {
            gamepad->PlayLeftRight(large, small, duration);
        }
    } else if (address == "/haptic/wheel/constant") {
        float strength = 0.0f;
        int duration = 0;

        if (arg_idx < typeTags.size()) strength = getNextArg(typeTags[arg_idx++]);
        if (arg_idx < typeTags.size()) duration = (int)getNextArg(typeTags[arg_idx++]);

        if (auto* wheel = dynamic_cast<SteeringWheelHaptics*>(haptic_device)) {
            wheel->PlayConstant(strength, duration);
        }
    }
    // Add other effects as needed
}
