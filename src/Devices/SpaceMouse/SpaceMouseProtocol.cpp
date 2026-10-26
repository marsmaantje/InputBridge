// src/Devices/SpaceMouse/SpaceMouseProtocol.cpp
#include "SpaceMouseProtocol.h"
#include <algorithm>

namespace InputBridge::SpaceMouse {

namespace {
constexpr ModelInfo kModels[] = {
    // Logitech-era devices
    {kVendorLogitech, 0xC603, "SpaceMouse Plus XT",            21},
    {kVendorLogitech, 0xC605, "CadMan",                        2},
    {kVendorLogitech, 0xC606, "SpaceMouse Classic",            12},
    {kVendorLogitech, 0xC621, "Spaceball 5000",                12},
    {kVendorLogitech, 0xC623, "SpaceTraveler",                 8},
    {kVendorLogitech, 0xC625, "SpacePilot",                    21},
    {kVendorLogitech, 0xC626, "SpaceNavigator",                2},
    {kVendorLogitech, 0xC627, "SpaceExplorer",                 15},
    {kVendorLogitech, 0xC628, "SpaceNavigator for Notebooks",  2},
    {kVendorLogitech, 0xC629, "SpacePilot Pro",                31},
    {kVendorLogitech, 0xC62B, "SpaceMouse Pro",                15},
    // 3Dconnexion-era devices
    {kVendor3Dconnexion, 0xC62E, "SpaceMouse Wireless",               2},
    {kVendor3Dconnexion, 0xC62F, "SpaceMouse Wireless Receiver",      2},
    {kVendor3Dconnexion, 0xC631, "SpaceMouse Pro Wireless",           15},
    {kVendor3Dconnexion, 0xC632, "SpaceMouse Pro Wireless Receiver",  15},
    {kVendor3Dconnexion, 0xC633, "SpaceMouse Enterprise",             31},
    {kVendor3Dconnexion, 0xC635, "SpaceMouse Compact",                2},
    {kVendor3Dconnexion, 0xC636, "SpaceMouse Module",                 32},
    {kVendor3Dconnexion, 0xC652, "Universal Receiver",                32},
};

constexpr ModelInfo kGenericModel = {kVendor3Dconnexion, 0, "3D Mouse", kMaxButtons};

inline int16_t Le16(const uint8_t *p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
} // namespace

const ModelInfo *FindKnownModel(uint16_t vid, uint16_t pid) {
    for (const auto &m : kModels)
        if (m.vendor_id == vid && m.product_id == pid) return &m;
    return nullptr;
}

const ModelInfo *FindModelByName(const std::string &name) {
    for (const auto &m : kModels)
        if (name == m.name) return &m;
    return nullptr;
}

std::optional<ModelInfo> IdentifyDevice(uint16_t vid, uint16_t pid,
                                        uint16_t usage_page, uint16_t usage) {
    const bool usage_known = (usage_page != 0 || usage != 0);
    const bool multi_axis  = usage_page == kUsagePageGenericDesktop &&
                             usage == kUsageMultiAxisController;

    if (const ModelInfo *known = FindKnownModel(vid, pid)) {
        // A known PID exposes several interfaces on some models (LEDs,
        // vendor data...). Only the motion interface is ours.
        if (!usage_known || multi_axis) return *known;
        return std::nullopt;
    }
    if (vid == kVendor3Dconnexion && multi_axis) {
        ModelInfo m = kGenericModel;
        m.product_id = pid;
        return m;
    }
    return std::nullopt;
}

std::string ButtonName(const ModelInfo &model, int button) {
    // Only the two-button models have unambiguous names; bit order on the
    // larger models differs per model, so those stay numbered.
    if (model.buttons == 2) {
        if (button == 0) return "Left Button";
        if (button == 1) return "Right Button";
    }
    return "Button " + std::to_string(button);
}

ReportKind DecodeReport(const uint8_t *buf, size_t len, State &state) {
    if (!buf || len < 2) return ReportKind::Ignored;
    switch (buf[0]) {
    case 0x01:
        if (len >= 13) { // combined translation + rotation
            for (int i = 0; i < 6; ++i) state.raw[i] = Le16(buf + 1 + i * 2);
            return ReportKind::Combined;
        }
        if (len >= 7) {
            for (int i = 0; i < 3; ++i) state.raw[i] = Le16(buf + 1 + i * 2);
            return ReportKind::Translation;
        }
        return ReportKind::Ignored;
    case 0x02:
        if (len < 7) return ReportKind::Ignored;
        for (int i = 0; i < 3; ++i) state.raw[3 + i] = Le16(buf + 1 + i * 2);
        return ReportKind::Rotation;
    case 0x03: {
        uint32_t mask = 0;
        const size_t n = std::min<size_t>(len - 1, 4);
        for (size_t i = 0; i < n; ++i) mask |= static_cast<uint32_t>(buf[1 + i]) << (8 * i);
        state.buttons = mask;
        return ReportKind::Buttons;
    }
    case 0x17:
        state.battery_percent = std::min<int>(buf[1], 100);
        return ReportKind::Battery;
    default:
        return ReportKind::Ignored;
    }
}

float NormalizeAxis(int16_t raw, float full_scale) {
    if (full_scale <= 0.f) return 0.f;
    return std::clamp(static_cast<float>(raw) / full_scale, -1.f, 1.f);
}

} // namespace InputBridge::SpaceMouse
