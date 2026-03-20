#include "Network/OSCSubchannel.h"

SubchannelPath ParseSubchannelPath(std::string_view path) noexcept {
    SubchannelPath result;

    // ── 1. Must begin with "/haptic/" ─────────────────────────────────────────
    static constexpr std::string_view kHapticPrefix = "/haptic/";
    if (!path.starts_with(kHapticPrefix)) return result;

    // ── 2. Must have a slash *after* the prefix (i.e. at least one '/' beyond
    //       the fixed prefix — that is the separator between effect and slot). ──
    const auto last_slash = path.rfind('/');
    if (last_slash == std::string_view::npos
            || last_slash < kHapticPrefix.size() - 1)
        return result;

    // ── 3. The tail (after the last '/') must be a non-empty digit string ─────
    const auto tail = path.substr(last_slash + 1);
    if (tail.empty()) return result;
    for (char c : tail) {
        if (c < '0' || c > '9') return result;  // not a decimal digit
    }

    // ── 4. Convert the digit string to an int slot number ─────────────────────
    int slot = 0;
    for (char c : tail) slot = slot * 10 + (c - '0');

    // ── 5. Match the base path (everything up to the last slash) to a known
    //       effect name ─────────────────────────────────────────────────────────
    const auto base = path.substr(0, last_slash);

    if      (base == "/haptic/rumble")    result.effect = SubchannelPath::Effect::Rumble;
    else if (base == "/haptic/constant")  result.effect = SubchannelPath::Effect::Constant;
    else if (base == "/haptic/periodic")  result.effect = SubchannelPath::Effect::Periodic;
    else if (base == "/haptic/condition") result.effect = SubchannelPath::Effect::Condition;
    else return result;  // unknown effect — not a recognised subchannel path

    result.slot  = slot;
    result.valid = true;
    return result;
}
