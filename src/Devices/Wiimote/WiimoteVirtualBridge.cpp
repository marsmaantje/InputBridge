// src/Devices/Wiimote/WiimoteVirtualBridge.cpp
#include "WiimoteVirtualBridge.h"
#include "App/Log.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>

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
    constexpr float kZeroG = 512.f;
    constexpr float kOneGCounts = 128.f;
    return (float(raw) - kZeroG) / kOneGCounts;
}
} // namespace

// -- Name lookups (declared in the header, shared with InputLabelProvider) --
const char *WiimoteBridgeAxisName(int axis) {
    switch (static_cast<WiimoteAxis>(axis)) {
        case WiimoteAxis::Axis_AccelX:         return "Accel X";
        case WiimoteAxis::Axis_AccelY:         return "Accel Y";
        case WiimoteAxis::Axis_AccelZ:         return "Accel Z";
        case WiimoteAxis::Axis_IR1X:           return "IR Dot 1 X";
        case WiimoteAxis::Axis_IR1Y:           return "IR Dot 1 Y";
        case WiimoteAxis::Axis_IR2X:           return "IR Dot 2 X";
        case WiimoteAxis::Axis_IR2Y:           return "IR Dot 2 Y";
        case WiimoteAxis::Axis_IR3X:           return "IR Dot 3 X";
        case WiimoteAxis::Axis_IR3Y:           return "IR Dot 3 Y";
        case WiimoteAxis::Axis_IR4X:           return "IR Dot 4 X";
        case WiimoteAxis::Axis_IR4Y:           return "IR Dot 4 Y";
        case WiimoteAxis::Axis_IR1Size:        return "IR Dot 1 Size";
        case WiimoteAxis::Axis_IR2Size:        return "IR Dot 2 Size";
        case WiimoteAxis::Axis_IR3Size:        return "IR Dot 3 Size";
        case WiimoteAxis::Axis_IR4Size:        return "IR Dot 4 Size";
        case WiimoteAxis::Axis_NunchukX:       return "Nunchuk Stick X";
        case WiimoteAxis::Axis_NunchukY:       return "Nunchuk Stick Y";
        case WiimoteAxis::Axis_NunchukAccelX:  return "Nunchuk Accel X";
        case WiimoteAxis::Axis_NunchukAccelY:  return "Nunchuk Accel Y";
        case WiimoteAxis::Axis_NunchukAccelZ:  return "Nunchuk Accel Z";
        case WiimoteAxis::Axis_ClassicLX:      return "Classic Left Stick X";
        case WiimoteAxis::Axis_ClassicLY:      return "Classic Left Stick Y";
        case WiimoteAxis::Axis_ClassicRX:      return "Classic Right Stick X";
        case WiimoteAxis::Axis_ClassicRY:      return "Classic Right Stick Y";
        case WiimoteAxis::Axis_ClassicLTrigger: return "Classic Left Trigger";
        case WiimoteAxis::Axis_ClassicRTrigger: return "Classic Right Trigger";
        case WiimoteAxis::Axis_MotionPlusYaw:  return "Motion Plus Yaw";
        case WiimoteAxis::Axis_MotionPlusPitch: return "Motion Plus Pitch";
        case WiimoteAxis::Axis_MotionPlusRoll: return "Motion Plus Roll";
        case WiimoteAxis::Axis_Battery:        return "Battery Level";
        default: return nullptr;
    }
}

const char *WiimoteBridgeButtonName(int button) {
    switch (static_cast<WiimoteButton>(button)) {
        case WiimoteButton::Btn_A:            return "A";
        case WiimoteButton::Btn_B:            return "B";
        case WiimoteButton::Btn_One:          return "1";
        case WiimoteButton::Btn_Two:          return "2";
        case WiimoteButton::Btn_Plus:         return "+";
        case WiimoteButton::Btn_Minus:        return "-";
        case WiimoteButton::Btn_Home:         return "Home";
        case WiimoteButton::Btn_NunchukC:     return "Nunchuk C";
        case WiimoteButton::Btn_NunchukZ:     return "Nunchuk Z";
        case WiimoteButton::Btn_ClassicA:     return "Classic A";
        case WiimoteButton::Btn_ClassicB:     return "Classic B";
        case WiimoteButton::Btn_ClassicX:     return "Classic X";
        case WiimoteButton::Btn_ClassicY:     return "Classic Y";
        case WiimoteButton::Btn_ClassicL:     return "Classic L";
        case WiimoteButton::Btn_ClassicR:     return "Classic R";
        case WiimoteButton::Btn_ClassicZL:    return "Classic ZL";
        case WiimoteButton::Btn_ClassicZR:    return "Classic ZR";
        case WiimoteButton::Btn_ClassicUp:    return "Classic D-Pad Up";
        case WiimoteButton::Btn_ClassicDown:  return "Classic D-Pad Down";
        case WiimoteButton::Btn_ClassicLeft:  return "Classic D-Pad Left";
        case WiimoteButton::Btn_ClassicRight: return "Classic D-Pad Right";
        case WiimoteButton::Btn_ClassicPlus:  return "Classic +";
        case WiimoteButton::Btn_ClassicMinus: return "Classic -";
        case WiimoteButton::Btn_ClassicHome:  return "Classic Home";
        default: return nullptr;
    }
}

const char *WiimoteBridgeHatName(int hat) {
    switch (static_cast<WiimoteHat>(hat)) {
        case WiimoteHat::Hat_DPad:        return "D-Pad";
        case WiimoteHat::Hat_ClassicDPad: return "Classic D-Pad";
        default: return nullptr;
    }
}

const char *BalanceBoardBridgeAxisName(int axis) {
    switch (static_cast<BalanceAxis>(axis)) {
        case BalanceAxis::BAxis_TopLeft:     return "Top Left";
        case BalanceAxis::BAxis_TopRight:    return "Top Right";
        case BalanceAxis::BAxis_BottomLeft:  return "Bottom Left";
        case BalanceAxis::BAxis_BottomRight: return "Bottom Right";
        case BalanceAxis::BAxis_Total:       return "Total Weight";
        case BalanceAxis::BAxis_CoGX:        return "Center of Gravity X";
        case BalanceAxis::BAxis_CoGY:        return "Center of Gravity Y";
        case BalanceAxis::BAxis_Battery:     return "Battery Level";
        default: return nullptr;
    }
}

const char *BalanceBoardBridgeButtonName(int button) {
    switch (static_cast<BalanceButton>(button)) {
        case BalanceButton::BBtn_A: return "A";
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

namespace {
// Reads the *actual* name SDL attached the virtual joystick under, rather
// than trusting Entry::is_balance_board (which is only ever set by Attach()
// itself, so comparing a cached copy of it against the value that produced
// it can never disagree). This is the same identity InputLabelProvider
// keys off of to know which axis/button name table to use for an
// already-created SDL device - if that ever drifted from Entry's cached
// bool, InputLabelProvider's labels would be wrong for reasons this class
// couldn't see. Querying SDL directly keeps this check honest against the
// live device instead of another copy of our own state.
//
// Returns std::nullopt if the joystick's name can't be read at all (null
// joystick pointer, or SDL_GetJoystickName() itself returning null) - the
// caller falls back to Entry::is_balance_board in that case rather than
// treating an inconclusive read as "not a balance board".
std::optional<bool> AttachedAsBalanceBoard(SDL_Joystick *joystick) {
    const char *name = joystick ? SDL_GetJoystickName(joystick) : nullptr;
    if (!name) return std::nullopt;
    return std::strcmp(name, kBalanceBoardBridgeDeviceName) == 0;
}
} // namespace

void WiimoteVirtualBridge::Sync(const std::vector<std::unique_ptr<WiimoteDevice>> &wiimotes) {
    for (auto &dev : wiimotes) {
        const std::string &path = dev->Snapshot().hid_path;
        Entry *existing = Find(path);
        if (!existing) {
            Attach(*dev);
            continue;
        }

        // The initial is_balance_board hint comes from the Bluetooth HID
        // product string, which many stacks (notably on Windows) leave
        // blank/generic for a Balance Board - see WiimoteDevice::Init()'s
        // "self-heals" comment. That means Attach() can run before the
        // authoritative extension ID (0x0402) has been read, latching in
        // an SDL_JOYSTICK_TYPE_UNKNOWN virtual joystick shaped like a
        // regular Wii Remote. Once the snapshot's flag flips, re-attach so
        // the virtual device gets the right axis/button layout and name
        // too instead of staying wrong for the rest of the connection.
        //
        // Compare against the *actual* SDL joystick name (the same
        // identity InputLabelProvider keys off of) rather than the cached
        // Entry::is_balance_board bool alone - that bool is only ever set
        // by Attach() from this same snapshot flag, so checking it here
        // would just compare a value against a copy of itself and could
        // never catch drift between the live SDL device and what we think
        // we attached. Fall back to the cached bool only if the name can't
        // be read at all.
        const bool attached_as_balance = AttachedAsBalanceBoard(existing->joystick)
                                         .value_or(existing->is_balance_board);
        if (attached_as_balance != dev->Snapshot().is_balance_board) {
            LOG_INFO(kTag, "Wiimote '%s' balance-board classification changed "
                      "(%s -> %s) after the virtual joystick was already "
                      "created - re-attaching with the correct device type",
                      path.c_str(),
                      attached_as_balance ? "balance board" : "wiimote",
                      dev->Snapshot().is_balance_board ? "balance board" : "wiimote");
            Detach(*existing);
            m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(),
                [&](const Entry &e) { return e.hid_path == path; }),
                m_Entries.end());
            Attach(*dev);
        }
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

        // Generic so they take WiimoteAxis/BalanceAxis (resp. WiimoteButton/
        // BalanceButton); the enum -> SDL int index conversion lives here only.
        auto setAxis = [&](auto id, float bipolar) {
            const Sint16 raw = static_cast<Sint16>(std::clamp(bipolar, -1.f, 1.f) * 32767.0f);
            SDL_SetJoystickVirtualAxis(e->joystick, static_cast<int>(id), raw);
        };
        auto setBtn = [&](auto id, bool v) {
            SDL_SetJoystickVirtualButton(e->joystick, static_cast<int>(id), v);
        };
        auto setHat = [&](WiimoteHat id, Uint8 v) {
            SDL_SetJoystickVirtualHat(e->joystick, static_cast<int>(id), v);
        };
        if (snap.is_balance_board) {
            const auto &bb = snap.balance_board;
            setAxis(BalanceAxis::BAxis_TopLeft,     Norm01ToBipolar(bb.kg_top_left,     0.f, kBalanceMaxKgPerCorner));
            setAxis(BalanceAxis::BAxis_TopRight,    Norm01ToBipolar(bb.kg_top_right,    0.f, kBalanceMaxKgPerCorner));
            setAxis(BalanceAxis::BAxis_BottomLeft,  Norm01ToBipolar(bb.kg_bottom_left,  0.f, kBalanceMaxKgPerCorner));
            setAxis(BalanceAxis::BAxis_BottomRight, Norm01ToBipolar(bb.kg_bottom_right, 0.f, kBalanceMaxKgPerCorner));
            setAxis(BalanceAxis::BAxis_Total,       Norm01ToBipolar(bb.kg_total,        0.f, kBalanceMaxTotalKg));
            setAxis(BalanceAxis::BAxis_CoGX, std::clamp(bb.cog_x, -1.f, 1.f));
            setAxis(BalanceAxis::BAxis_CoGY, std::clamp(bb.cog_y, -1.f, 1.f));
            // Balance Board keeps its own raw battery byte (unlike a
            // handheld Wiimote), so use it directly for finer resolution.
            setAxis(BalanceAxis::BAxis_Battery, Norm01ToBipolar(float(bb.battery_raw), 0.f, kBatteryRawMax));
            setBtn(BalanceButton::BBtn_A, bb.button_a);
            continue;
        }

        setAxis(WiimoteAxis::Axis_AccelX, NormSymmetric(snap.accel.g_x, kAccelMaxG));
        setAxis(WiimoteAxis::Axis_AccelY, NormSymmetric(snap.accel.g_y, kAccelMaxG));
        setAxis(WiimoteAxis::Axis_AccelZ, NormSymmetric(snap.accel.g_z, kAccelMaxG));

        // IR: all 4 points bridged as X/Y axis pairs, in report slot order
        // - "dot 1" just means "whatever is in ir[0] right now", no
        // persistent identity across frames. Rests at center (0), not a
        // rail, when its slot isn't visible.
        static constexpr std::array<WiimoteAxis, 4> kIRAxisX = {WiimoteAxis::Axis_IR1X, WiimoteAxis::Axis_IR2X, WiimoteAxis::Axis_IR3X, WiimoteAxis::Axis_IR4X};
        static constexpr std::array<WiimoteAxis, 4> kIRAxisY = {WiimoteAxis::Axis_IR1Y, WiimoteAxis::Axis_IR2Y, WiimoteAxis::Axis_IR3Y, WiimoteAxis::Axis_IR4Y};
        for (std::size_t i = 0; i < kIRAxisX.size(); ++i) {
            const auto &dot = snap.ir[i];
            setAxis(kIRAxisX[i], dot.visible ? (float(dot.x) / 1023.0f) * 2.f - 1.f : 0.f);
            setAxis(kIRAxisY[i], dot.visible ? (float(dot.y) / 767.0f)  * 2.f - 1.f : 0.f);
        }

        // Dot size (0-15), only meaningful in IR Extended/Full mode
        // (WiimoteDevice::SetIRMode; stays 0 in Basic mode). Magnitude
        // with no natural sign, so uses the "rest = -1" convention like
        // the Balance Board weight axes: 0/not-visible -> -1, 15 -> +1.
        static constexpr std::array<WiimoteAxis, 4> kIRAxisSize = {WiimoteAxis::Axis_IR1Size, WiimoteAxis::Axis_IR2Size, WiimoteAxis::Axis_IR3Size, WiimoteAxis::Axis_IR4Size};
        for (std::size_t i = 0; i < kIRAxisSize.size(); ++i) {
            const auto &dot = snap.ir[i];
            setAxis(kIRAxisSize[i], dot.visible ? Norm01ToBipolar(float(dot.size), 0.f, 15.f) : -1.f);
        }

        // Nunchuk stick: 8-bit, center ~128, physical range roughly
        // 35-228 - ±100 half-range avoids needing per-device calibration.
        setAxis(WiimoteAxis::Axis_NunchukX, (float(snap.nunchuk.stick_x) - 128.f) / 100.f);
        setAxis(WiimoteAxis::Axis_NunchukY, (float(snap.nunchuk.stick_y) - 128.f) / 100.f);

        // Nunchuk accel: same nominal 0g/1g scale as the Wiimote's own
        // (NunchukAccelRawToG above), so reuse kAccelMaxG.
        setAxis(WiimoteAxis::Axis_NunchukAccelX, NormSymmetric(NunchukAccelRawToG(snap.nunchuk.accel_x), kAccelMaxG));
        setAxis(WiimoteAxis::Axis_NunchukAccelY, NormSymmetric(NunchukAccelRawToG(snap.nunchuk.accel_y), kAccelMaxG));
        setAxis(WiimoteAxis::Axis_NunchukAccelZ, NormSymmetric(NunchukAccelRawToG(snap.nunchuk.accel_z), kAccelMaxG));

        // Classic Controller sticks: 6-bit (0-63, center 32) left, 5-bit
        // (0-31, center 16) right - see ClassicControllerState/Decode::Classic.
        setAxis(WiimoteAxis::Axis_ClassicLX, (float(snap.classic.left_x)  - 32.f) / 32.f);
        setAxis(WiimoteAxis::Axis_ClassicLY, (float(snap.classic.left_y)  - 32.f) / 32.f);
        setAxis(WiimoteAxis::Axis_ClassicRX, (float(snap.classic.right_x) - 16.f) / 16.f);
        setAxis(WiimoteAxis::Axis_ClassicRY, (float(snap.classic.right_y) - 16.f) / 16.f);

        // Classic Controller triggers: 5-bit analog (0-31), digital 0/31 on
        // Pro - see ClassicControllerState's comment. Same "rest = -1, full
        // = +1" convention as the other one-directional analogs above
        // (IR dot size, Balance Board weight).
        setAxis(WiimoteAxis::Axis_ClassicLTrigger, Norm01ToBipolar(float(snap.classic.left_trigger),  0.f, 31.f));
        setAxis(WiimoteAxis::Axis_ClassicRTrigger, Norm01ToBipolar(float(snap.classic.right_trigger), 0.f, 31.f));

        setAxis(WiimoteAxis::Axis_MotionPlusYaw,   NormSymmetric(snap.motion_plus.deg_s_yaw,   kMotionPlusMaxDegPerSec));
        setAxis(WiimoteAxis::Axis_MotionPlusPitch, NormSymmetric(snap.motion_plus.deg_s_pitch, kMotionPlusMaxDegPerSec));
        setAxis(WiimoteAxis::Axis_MotionPlusRoll,  NormSymmetric(snap.motion_plus.deg_s_roll,  kMotionPlusMaxDegPerSec));

        // Battery: -1 = empty, +1 = full, same "rest = -1" magnitude
        // convention as the IR dot-size axes above (see their comment).
        setAxis(WiimoteAxis::Axis_Battery, BatteryBarsToRaw01(snap.battery) * 2.f - 1.f);

        setBtn(WiimoteButton::Btn_A, snap.core.a);         setBtn(WiimoteButton::Btn_B, snap.core.b);
        setBtn(WiimoteButton::Btn_One, snap.core.one);     setBtn(WiimoteButton::Btn_Two, snap.core.two);
        setBtn(WiimoteButton::Btn_Plus, snap.core.plus);   setBtn(WiimoteButton::Btn_Minus, snap.core.minus);
        setBtn(WiimoteButton::Btn_Home, snap.core.home);

        // D-Pad as a hat (see WiimoteHat) - bits OR directly into SDL's
        // hat bitmask, so a diagonal reads as e.g. SDL_HAT_LEFTUP for free.
        Uint8 dpad_hat = SDL_HAT_CENTERED;
        if (snap.core.up)    dpad_hat |= SDL_HAT_UP;
        if (snap.core.down)  dpad_hat |= SDL_HAT_DOWN;
        if (snap.core.left)  dpad_hat |= SDL_HAT_LEFT;
        if (snap.core.right) dpad_hat |= SDL_HAT_RIGHT;
        setHat(WiimoteHat::Hat_DPad, dpad_hat);

        setBtn(WiimoteButton::Btn_NunchukC, snap.nunchuk.button_c);
        setBtn(WiimoteButton::Btn_NunchukZ, snap.nunchuk.button_z);

        setBtn(WiimoteButton::Btn_ClassicA, snap.classic.a); setBtn(WiimoteButton::Btn_ClassicB, snap.classic.b);
        setBtn(WiimoteButton::Btn_ClassicX, snap.classic.x); setBtn(WiimoteButton::Btn_ClassicY, snap.classic.y);
        setBtn(WiimoteButton::Btn_ClassicL, snap.classic.l); setBtn(WiimoteButton::Btn_ClassicR, snap.classic.r);
        setBtn(WiimoteButton::Btn_ClassicZL, snap.classic.zl); setBtn(WiimoteButton::Btn_ClassicZR, snap.classic.zr);

        // Classic Controller D-Pad as a hat (see WiimoteHat)
        Uint8 classic_dpad_hat = SDL_HAT_CENTERED;
        if (snap.classic.dpad_up)    classic_dpad_hat |= SDL_HAT_UP;
        if (snap.classic.dpad_down)  classic_dpad_hat |= SDL_HAT_DOWN;
        if (snap.classic.dpad_left)  classic_dpad_hat |= SDL_HAT_LEFT;
        if (snap.classic.dpad_right) classic_dpad_hat |= SDL_HAT_RIGHT;
        setHat(WiimoteHat::Hat_ClassicDPad, classic_dpad_hat);

        setBtn(WiimoteButton::Btn_ClassicUp, snap.classic.dpad_up);
        setBtn(WiimoteButton::Btn_ClassicDown, snap.classic.dpad_down);
        setBtn(WiimoteButton::Btn_ClassicLeft, snap.classic.dpad_left);
        setBtn(WiimoteButton::Btn_ClassicRight, snap.classic.dpad_right);
        setBtn(WiimoteButton::Btn_ClassicPlus, snap.classic.plus);
        setBtn(WiimoteButton::Btn_ClassicMinus, snap.classic.minus);
        setBtn(WiimoteButton::Btn_ClassicHome, snap.classic.home);
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
