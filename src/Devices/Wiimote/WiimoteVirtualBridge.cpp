// src/Devices/Wiimote/WiimoteVirtualBridge.cpp
#include "WiimoteVirtualBridge.h"
#include "App/Log.h"
#include <algorithm>

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "WiimoteVirtualBridge";

// IMPORTANT: the bridge device names (see header) must never contain "Wii
// Remote", "RVL-CNT", or "RVL-WBC" - DeviceManager's Wiimote-family filter
// would mistake this virtual joystick for a second real Wiimote. Must also
// avoid "Nintendo"/"Wiimote" - DevicePanel's legacy WiimoteVisualizer tab
// keys off those substrings and assumes SDL's own Wii button layout, which
// doesn't match this bridge's custom layout.

// Balance Board weight axes rest at 0kg -> -1, matching the "rest = -1"
// convention VirtualDeviceManager's gamepad/wheel presets use for
// throttle/brake/triggers.
constexpr float kBalanceMaxKgPerCorner = 80.0f;  // generous single-corner max
constexpr float kBalanceMaxTotalKg     = 150.0f; // above typical adult body weight

// Motion Plus deg/s -> [-1,1], clamped well below the ~±2000 deg/s fast-mode
// range since most mapping uses (aiming, tilt gestures) want a smaller
// working range; 500 deg/s is already a brisk wrist flick.
constexpr float kMotionPlusMaxDegPerSec = 500.0f;

// A handheld remote's accelerometer rarely exceeds ±3g in normal use
// (the sensor itself saturates near there) - use that as full scale.
constexpr float kAccelMaxG = 3.0f;

// Balance Board keeps its raw 0x00-0xFF battery byte, mapped straight to
// -1..+1 (0xFF -> +1) rather than clamped at the "4 bars" threshold, so the
// axis resolves finer differences than the 4-bar icon shows.
constexpr float kBatteryRawMax = 255.0f;

// A handheld Wiimote only keeps the *classified* BatteryBars, not the raw
// byte, so derive its axis from the same thresholds
// ClassifyWiimoteBattery() (WiimoteState.h) uses for the UI's 4-bar icon,
// keeping axis and icon consistent. Returns each bracket's midpoint as
// [0,1].
float BatteryBarsToRaw01(BatteryBars bars) {
    switch (bars) {
        case BatteryBars::Four:  return 1.00f; // >= 0x82
        case BatteryBars::Three: return 0.80f; // 0x7D-0x81
        case BatteryBars::Two:   return 0.60f; // 0x78-0x7C
        case BatteryBars::One:   return 0.40f; // 0x6A-0x77
        case BatteryBars::Empty:
        default:                 return 0.10f; // < 0x6A, still show a sliver rather than a hard 0
    }
}

float Norm01ToBipolar(float v, float lo, float hi) {
    if (hi <= lo) return -1.f;
    const float t = std::clamp((v - lo) / (hi - lo), 0.f, 1.f); // 0..1
    return t * 2.f - 1.f; // -1..1, rests at -1 when v==lo
}
float NormSymmetric(float v, float maxAbs) {
    if (maxAbs <= 0.f) return 0.f;
    return std::clamp(v / maxAbs, -1.f, 1.f);
}

// NunchukState only keeps raw 10-bit accel counts (same scale as the
// Wiimote's own accel), so apply the same nominal 0g/1g conversion
// WiimoteDecoder::Accel() uses before feeding NormSymmetric/kAccelMaxG.
float NunchukAccelRawToG(uint16_t raw) {
    constexpr float kZeroG = 512.f, kOneGCounts = 128.f;
    return (float(raw) - kZeroG) / kOneGCounts;
}
} // namespace

// -- Name lookups (declared in the header, shared with InputLabelProvider) --
const char *WiimoteBridgeAxisName(int axis) {
    switch (axis) {
        case Axis_AccelX:         return "Accel X";
        case Axis_AccelY:         return "Accel Y";
        case Axis_AccelZ:         return "Accel Z";
        case Axis_IR1X:           return "IR Dot 1 X";
        case Axis_IR1Y:           return "IR Dot 1 Y";
        case Axis_IR2X:           return "IR Dot 2 X";
        case Axis_IR2Y:           return "IR Dot 2 Y";
        case Axis_IR3X:           return "IR Dot 3 X";
        case Axis_IR3Y:           return "IR Dot 3 Y";
        case Axis_IR4X:           return "IR Dot 4 X";
        case Axis_IR4Y:           return "IR Dot 4 Y";
        case Axis_IR1Size:        return "IR Dot 1 Size";
        case Axis_IR2Size:        return "IR Dot 2 Size";
        case Axis_IR3Size:        return "IR Dot 3 Size";
        case Axis_IR4Size:        return "IR Dot 4 Size";
        case Axis_NunchukX:       return "Nunchuk Stick X";
        case Axis_NunchukY:       return "Nunchuk Stick Y";
        case Axis_NunchukAccelX:  return "Nunchuk Accel X";
        case Axis_NunchukAccelY:  return "Nunchuk Accel Y";
        case Axis_NunchukAccelZ:  return "Nunchuk Accel Z";
        case Axis_ClassicLX:      return "Classic Left Stick X";
        case Axis_ClassicLY:      return "Classic Left Stick Y";
        case Axis_ClassicRX:      return "Classic Right Stick X";
        case Axis_ClassicRY:      return "Classic Right Stick Y";
        case Axis_MotionPlusYaw:  return "Motion Plus Yaw";
        case Axis_MotionPlusPitch: return "Motion Plus Pitch";
        case Axis_MotionPlusRoll: return "Motion Plus Roll";
        case Axis_Battery:        return "Battery Level";
        default: return nullptr;
    }
}

const char *WiimoteBridgeButtonName(int button) {
    switch (button) {
        case Btn_A:            return "A";
        case Btn_B:            return "B";
        case Btn_One:          return "1";
        case Btn_Two:          return "2";
        case Btn_Plus:         return "+";
        case Btn_Minus:        return "-";
        case Btn_Home:         return "Home";
        case Btn_NunchukC:     return "Nunchuk C";
        case Btn_NunchukZ:     return "Nunchuk Z";
        case Btn_ClassicA:     return "Classic A";
        case Btn_ClassicB:     return "Classic B";
        case Btn_ClassicX:     return "Classic X";
        case Btn_ClassicY:     return "Classic Y";
        case Btn_ClassicL:     return "Classic L";
        case Btn_ClassicR:     return "Classic R";
        case Btn_ClassicZL:    return "Classic ZL";
        case Btn_ClassicZR:    return "Classic ZR";
        case Btn_ClassicUp:    return "Classic D-Pad Up";
        case Btn_ClassicDown:  return "Classic D-Pad Down";
        case Btn_ClassicLeft:  return "Classic D-Pad Left";
        case Btn_ClassicRight: return "Classic D-Pad Right";
        default: return nullptr;
    }
}

const char *WiimoteBridgeHatName(int hat) {
    switch (hat) {
        case Hat_DPad: return "D-Pad";
        default: return nullptr;
    }
}

const char *BalanceBoardBridgeAxisName(int axis) {
    switch (axis) {
        case BAxis_TopLeft:     return "Top Left";
        case BAxis_TopRight:    return "Top Right";
        case BAxis_BottomLeft:  return "Bottom Left";
        case BAxis_BottomRight: return "Bottom Right";
        case BAxis_Total:       return "Total Weight";
        case BAxis_CoGX:        return "Center of Gravity X";
        case BAxis_CoGY:        return "Center of Gravity Y";
        case BAxis_Battery:     return "Battery Level";
        default: return nullptr;
    }
}

const char *BalanceBoardBridgeButtonName(int button) {
    switch (button) {
        case BBtn_A: return "A";
        default: return nullptr;
    }
}

WiimoteVirtualBridge &WiimoteVirtualBridge::GetInstance() {
    static WiimoteVirtualBridge instance;
    return instance;
}

WiimoteVirtualBridge::Entry *WiimoteVirtualBridge::Find(const std::string &hid_path) {
    for (auto &e : m_Entries)
        if (e.hid_path == hid_path) return &e;
    return nullptr;
}

void WiimoteVirtualBridge::Attach(const WiimoteDevice &dev) {
    const auto &snap = dev.Snapshot();
    const bool balance = snap.is_balance_board;

    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    // NOT SDL_JOYSTICK_TYPE_GAMEPAD: that would route through
    // SDL_OpenGamepad(), and InputLabelProvider would then label axes
    // using SDL's default gamepad mapping by numeric position (e.g. "Left
    // Stick X") - which has no idea axis 0 here is actually accelerometer
    // X. SDL_JOYSTICK_TYPE_UNKNOWN keeps `gamepad` null (like
    // VirtualDeviceManager's other non-gamepad presets), so
    // InputLabelProvider's Wiimote-aware branch (matched by device name)
    // supplies the real per-index names instead.
    desc.type     = static_cast<Uint16>(SDL_JOYSTICK_TYPE_UNKNOWN);
    desc.naxes    = static_cast<Uint16>(balance ? kBalanceNumAxes : kWiimoteNumAxes);
    desc.nbuttons = static_cast<Uint16>(balance ? kBalanceNumButtons : kWiimoteNumButtons);
    desc.nhats    = static_cast<Uint16>(balance ? 0 : kWiimoteNumHats); // Balance Board has no D-Pad

    // Exact strings matter (see file-level comment); also referenced by
    // InputLabelProvider's Wiimote-aware branch - keep in sync if changed.
    const std::string name = balance ? kBalanceBoardBridgeDeviceName
                                      : kWiimoteBridgeDeviceName;
    desc.name = name.c_str();

    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        LOG_ERROR(kTag, "SDL_AttachVirtualJoystick failed for %s: %s",
                   snap.hid_path.c_str(), SDL_GetError());
        return;
    }
    SDL_Joystick *joystick = SDL_OpenJoystick(id);
    if (!joystick) {
        LOG_ERROR(kTag, "SDL_OpenJoystick failed for bridge joystick (%s): %s",
                   snap.hid_path.c_str(), SDL_GetError());
        SDL_DetachVirtualJoystick(id);
        return;
    }

    Entry e;
    e.hid_path = snap.hid_path;
    e.joystick_id = id;
    e.joystick = joystick;
    e.is_balance_board = balance;
    m_Entries.push_back(e);

    LOG_INFO(kTag, "Bridged Wiimote '%s' -> virtual joystick id=%u (%s)",
              snap.hid_path.c_str(), static_cast<unsigned>(id), name.c_str());
}

void WiimoteVirtualBridge::Detach(Entry &entry) {
    // SDL_DetachVirtualJoystick fires SDL_EVENT_JOYSTICK_REMOVED, which
    // DeviceManager::HandleDeviceRemoved() handles by closing the SDL
    // handle - don't also close it here (see VirtualDeviceManager::RemoveDevice).
    SDL_DetachVirtualJoystick(entry.joystick_id);
}

void WiimoteVirtualBridge::Sync(const std::vector<std::unique_ptr<WiimoteDevice>> &wiimotes) {
    for (auto &dev : wiimotes) {
        const std::string &path = dev->Snapshot().hid_path;
        if (!Find(path)) Attach(*dev);
    }

    auto still_present = [&](const std::string &path) {
        for (auto &dev : wiimotes)
            if (dev->Snapshot().hid_path == path) return true;
        return false;
    };
    auto it = std::remove_if(m_Entries.begin(), m_Entries.end(), [&](Entry &e) {
        if (still_present(e.hid_path)) return false;
        Detach(e);
        return true;
    });
    if (it != m_Entries.end())
        LOG_INFO(kTag, "Removing %td bridge joystick(s) for disconnected Wiimotes",
                  std::distance(it, m_Entries.end()));
    m_Entries.erase(it, m_Entries.end());
}

void WiimoteVirtualBridge::PushAllStates(const std::vector<std::unique_ptr<WiimoteDevice>> &wiimotes) {
    for (auto &dev : wiimotes) {
        Entry *e = Find(dev->Snapshot().hid_path);
        if (!e || !e->joystick) continue;
        const auto &snap = dev->Snapshot();

        auto setAxis = [&](int idx, float bipolar) {
            const Sint16 raw = static_cast<Sint16>(std::clamp(bipolar, -1.f, 1.f) * 32767.0f);
            SDL_SetJoystickVirtualAxis(e->joystick, idx, raw);
        };
        auto setBtn = [&](int idx, bool v) {
            SDL_SetJoystickVirtualButton(e->joystick, idx, v);
        };
        if (snap.is_balance_board) {
            const auto &bb = snap.balance_board;
            setAxis(BAxis_TopLeft,     Norm01ToBipolar(bb.kg_top_left,     0.f, kBalanceMaxKgPerCorner));
            setAxis(BAxis_TopRight,    Norm01ToBipolar(bb.kg_top_right,    0.f, kBalanceMaxKgPerCorner));
            setAxis(BAxis_BottomLeft,  Norm01ToBipolar(bb.kg_bottom_left,  0.f, kBalanceMaxKgPerCorner));
            setAxis(BAxis_BottomRight, Norm01ToBipolar(bb.kg_bottom_right, 0.f, kBalanceMaxKgPerCorner));
            setAxis(BAxis_Total,       Norm01ToBipolar(bb.kg_total,        0.f, kBalanceMaxTotalKg));
            setAxis(BAxis_CoGX, std::clamp(bb.cog_x, -1.f, 1.f));
            setAxis(BAxis_CoGY, std::clamp(bb.cog_y, -1.f, 1.f));
            // Balance Board keeps its own raw battery byte (unlike a
            // handheld Wiimote), so use it directly for finer resolution.
            setAxis(BAxis_Battery, Norm01ToBipolar(float(bb.battery_raw), 0.f, kBatteryRawMax));
            setBtn(BBtn_A, bb.button_a);
            continue;
        }

        setAxis(Axis_AccelX, NormSymmetric(snap.accel.g_x, kAccelMaxG));
        setAxis(Axis_AccelY, NormSymmetric(snap.accel.g_y, kAccelMaxG));
        setAxis(Axis_AccelZ, NormSymmetric(snap.accel.g_z, kAccelMaxG));

        // IR: all 4 points bridged as X/Y axis pairs, in report slot order
        // - "dot 1" just means "whatever is in ir[0] right now", no
        // persistent identity across frames. Rests at center (0), not a
        // rail, when its slot isn't visible.
        static constexpr int kIRAxisX[4] = {Axis_IR1X, Axis_IR2X, Axis_IR3X, Axis_IR4X};
        static constexpr int kIRAxisY[4] = {Axis_IR1Y, Axis_IR2Y, Axis_IR3Y, Axis_IR4Y};
        for (int i = 0; i < 4; ++i) {
            const auto &dot = snap.ir[i];
            setAxis(kIRAxisX[i], dot.visible ? (float(dot.x) / 1023.0f) * 2.f - 1.f : 0.f);
            setAxis(kIRAxisY[i], dot.visible ? (float(dot.y) / 767.0f)  * 2.f - 1.f : 0.f);
        }

        // Dot size (0-15), only meaningful in IR Extended mode
        // (WiimoteDevice::SetIRExtendedMode; stays 0 otherwise). Magnitude
        // with no natural sign, so uses the "rest = -1" convention like
        // the Balance Board weight axes: 0/not-visible -> -1, 15 -> +1.
        static constexpr int kIRAxisSize[4] = {Axis_IR1Size, Axis_IR2Size, Axis_IR3Size, Axis_IR4Size};
        for (int i = 0; i < 4; ++i) {
            const auto &dot = snap.ir[i];
            setAxis(kIRAxisSize[i], dot.visible ? Norm01ToBipolar(float(dot.size), 0.f, 15.f) : -1.f);
        }

        // Nunchuk stick: 8-bit, center ~128, physical range roughly
        // 35-228 - ±100 half-range avoids needing per-device calibration.
        setAxis(Axis_NunchukX, (float(snap.nunchuk.stick_x) - 128.f) / 100.f);
        setAxis(Axis_NunchukY, (float(snap.nunchuk.stick_y) - 128.f) / 100.f);

        // Nunchuk accel: same nominal 0g/1g scale as the Wiimote's own
        // (NunchukAccelRawToG above), so reuse kAccelMaxG.
        setAxis(Axis_NunchukAccelX, NormSymmetric(NunchukAccelRawToG(snap.nunchuk.accel_x), kAccelMaxG));
        setAxis(Axis_NunchukAccelY, NormSymmetric(NunchukAccelRawToG(snap.nunchuk.accel_y), kAccelMaxG));
        setAxis(Axis_NunchukAccelZ, NormSymmetric(NunchukAccelRawToG(snap.nunchuk.accel_z), kAccelMaxG));

        // Classic Controller sticks: 6-bit (0-63, center 32) left, 5-bit
        // (0-31, center 16) right - see ClassicControllerState/Decode::Classic.
        setAxis(Axis_ClassicLX, (float(snap.classic.left_x)  - 32.f) / 32.f);
        setAxis(Axis_ClassicLY, (float(snap.classic.left_y)  - 32.f) / 32.f);
        setAxis(Axis_ClassicRX, (float(snap.classic.right_x) - 16.f) / 16.f);
        setAxis(Axis_ClassicRY, (float(snap.classic.right_y) - 16.f) / 16.f);

        setAxis(Axis_MotionPlusYaw,   NormSymmetric(snap.motion_plus.deg_s_yaw,   kMotionPlusMaxDegPerSec));
        setAxis(Axis_MotionPlusPitch, NormSymmetric(snap.motion_plus.deg_s_pitch, kMotionPlusMaxDegPerSec));
        setAxis(Axis_MotionPlusRoll,  NormSymmetric(snap.motion_plus.deg_s_roll,  kMotionPlusMaxDegPerSec));

        // Battery: -1 = empty, +1 = full, same "rest = -1" magnitude
        // convention as the IR dot-size axes above (see their comment).
        setAxis(Axis_Battery, BatteryBarsToRaw01(snap.battery) * 2.f - 1.f);

        setBtn(Btn_A, snap.core.a);         setBtn(Btn_B, snap.core.b);
        setBtn(Btn_One, snap.core.one);     setBtn(Btn_Two, snap.core.two);
        setBtn(Btn_Plus, snap.core.plus);   setBtn(Btn_Minus, snap.core.minus);
        setBtn(Btn_Home, snap.core.home);

        // D-Pad as a hat (see WiimoteHat) - bits OR directly into SDL's
        // hat bitmask, so a diagonal reads as e.g. SDL_HAT_LEFTUP for free.
        Uint8 dpad_hat = SDL_HAT_CENTERED;
        if (snap.core.up)    dpad_hat |= SDL_HAT_UP;
        if (snap.core.down)  dpad_hat |= SDL_HAT_DOWN;
        if (snap.core.left)  dpad_hat |= SDL_HAT_LEFT;
        if (snap.core.right) dpad_hat |= SDL_HAT_RIGHT;
        SDL_SetJoystickVirtualHat(e->joystick, Hat_DPad, dpad_hat);

        setBtn(Btn_NunchukC, snap.nunchuk.button_c);
        setBtn(Btn_NunchukZ, snap.nunchuk.button_z);

        setBtn(Btn_ClassicA, snap.classic.a); setBtn(Btn_ClassicB, snap.classic.b);
        setBtn(Btn_ClassicX, snap.classic.x); setBtn(Btn_ClassicY, snap.classic.y);
        setBtn(Btn_ClassicL, snap.classic.l); setBtn(Btn_ClassicR, snap.classic.r);
        setBtn(Btn_ClassicZL, snap.classic.zl); setBtn(Btn_ClassicZR, snap.classic.zr);
        setBtn(Btn_ClassicUp, snap.classic.dpad_up);
        setBtn(Btn_ClassicDown, snap.classic.dpad_down);
        setBtn(Btn_ClassicLeft, snap.classic.dpad_left);
        setBtn(Btn_ClassicRight, snap.classic.dpad_right);
    }
}

void WiimoteVirtualBridge::RemoveAll() {
    for (auto &e : m_Entries) Detach(e);
    m_Entries.clear();
}

const std::string *WiimoteVirtualBridge::FindHidPathForJoystick(SDL_JoystickID joystick_id, bool *out_is_balance_board) const {
    for (const auto &e : m_Entries) {
        if (e.joystick_id == joystick_id) {
            if (out_is_balance_board) *out_is_balance_board = e.is_balance_board;
            return &e.hid_path;
        }
    }
    return nullptr;
}

std::vector<SDL_JoystickID> WiimoteVirtualBridge::GetAllJoystickIds() const {
    std::vector<SDL_JoystickID> ids;
    ids.reserve(m_Entries.size());
    for (const auto &e : m_Entries)
        if (e.joystick_id != 0) ids.push_back(e.joystick_id);
    return ids;
}

} // namespace InputBridge::Wiimote
