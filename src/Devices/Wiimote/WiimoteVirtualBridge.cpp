// src/Devices/Wiimote/WiimoteVirtualBridge.cpp
#include "WiimoteVirtualBridge.h"
#include "App/Log.h"
#include <algorithm>

namespace InputBridge::Wiimote {

namespace {
constexpr const char *kTag = "WiimoteVirtualBridge";

// IMPORTANT: kWiimoteBridgeDeviceName/kBalanceBoardBridgeDeviceName (see
// header) must never contain "Wii Remote", "RVL-CNT", or "RVL-WBC" -
// DeviceManager::HandleDeviceAdded's Wiimote-family filter (added to stop
// SDL's own HIDAPI Wii driver racing WiimoteManager for the real hardware)
// would silently reject this bridge's virtual joystick too, thinking it's
// a second real Wiimote. They also must not contain "Nintendo" or
// "Wiimote" - DevicePanel's legacy DeviceState-based WiimoteVisualizer tab
// keys off those substrings and assumes SDL's own Wii HIDAPI button
// layout, which doesn't match this bridge's custom layout and would show
// wrong button highlighting if triggered here.

// Balance Board weight axes rest at 0kg -> mapped to -1, matching the
// "trigger at rest = -1" convention VirtualDeviceManager's gamepad/wheel
// presets already use for throttle/brake/triggers (see the naming
// convention embedded in the Preset tables in VirtualDeviceManager.cpp).
constexpr float kBalanceMaxKgPerCorner = 80.0f;  // generous single-corner max
constexpr float kBalanceMaxTotalKg     = 150.0f; // above typical adult body weight

// Motion Plus deg/s -> [-1,1], clamped rather than scaled to the full
// documented ~±2000 deg/s fast-mode range: most mapping use cases (aiming,
// tilt gestures) care about a much smaller working range. 500 deg/s is
// already a brisk wrist flick.
constexpr float kMotionPlusMaxDegPerSec = 500.0f;

// A hand-held remote's accelerometer rarely sees much past ±3g in normal
// use (WiiBrew notes the sensor itself saturates around ±3g); use that as
// the mapping's full-scale range so a moderate shake reaches the rails.
constexpr float kAccelMaxG = 3.0f;

// Balance Board keeps its raw 0x00-0xFF battery byte around
// (snap.balance_board.battery_raw); Norm01ToBipolar(raw, 0, kBatteryRawMax)
// maps that straight to the axis's -1..+1 range, full scale (0xFF -> +1)
// rather than clamping at the "Four bars" threshold (0x82), so the axis
// keeps resolving differences a 4-bar icon can't show.
constexpr float kBatteryRawMax = 255.0f;

// A handheld Wiimote, unlike the Balance Board, only keeps the *classified*
// BatteryBars around (snap.battery - see WiimoteDevice.cpp's UpdateStatus()),
// not the raw byte it came from, so its axis is derived from the same
// bracket thresholds ClassifyWiimoteBattery() (WiimoteState.h) already
// uses for the UI's 4-bar icon, keeping axis and icon consistent with each
// other. Returns each bracket's approximate midpoint as a [0,1] fraction.
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
    // NOT SDL_JOYSTICK_TYPE_GAMEPAD: DeviceFactory routes anything typed as
    // a gamepad through SDL_OpenGamepad(), which gives DeviceState a
    // non-null `gamepad` handle - and InputLabelProvider (the "Raw Inputs"
    // tab's naming source, shown for every device including this one) uses
    // that to walk SDL_GetGamepadBindings() and label axes/buttons with
    // Xbox-style names ("Left Stick X", "South", ...) looked up by
    // *numeric position* in SDL's synthetic default gamepad mapping. That
    // table has no idea this virtual joystick's axis 0 actually holds
    // accelerometer X, axis 11 holds Motion Plus yaw, etc - hence the
    // reported name/channel mismatch. SDL_JOYSTICK_TYPE_UNKNOWN routes
    // through CreateGenericDevice() instead (gamepad left null, exactly
    // like VirtualDeviceManager's own non-gamepad presets), and
    // InputLabelProvider's Wiimote-aware branch below (matched by this
    // device's name) supplies the real per-index names directly instead of
    // falling through to a numbered "Axis N" fallback.
    desc.type     = static_cast<Uint16>(SDL_JOYSTICK_TYPE_UNKNOWN);
    desc.naxes    = static_cast<Uint16>(balance ? kBalanceNumAxes : kWiimoteNumAxes);
    desc.nbuttons = static_cast<Uint16>(balance ? kBalanceNumButtons : kWiimoteNumButtons);
    desc.nhats    = static_cast<Uint16>(balance ? 0 : kWiimoteNumHats); // Balance Board has no D-Pad

    // See the file-level comment for why these exact strings matter. Also
    // referenced by name (not substring) from InputLabelProvider's
    // Wiimote-aware branch - keep those two switch/if checks in sync if
    // either string changes.
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
    // Same ordering VirtualDeviceManager::RemoveDevice documents:
    // SDL_DetachVirtualJoystick fires SDL_EVENT_JOYSTICK_REMOVED, which
    // DeviceManager::HandleDeviceRemoved() handles by closing the SDL
    // handle itself - do not also close it here.
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
            // handheld Wiimote, which only surfaces the classified
            // BatteryBars - see WiimoteDevice.cpp), so use it directly for
            // slightly finer resolution than re-deriving from the 4-bar
            // classification would give.
            setAxis(BAxis_Battery, Norm01ToBipolar(float(bb.battery_raw), 0.f, kBatteryRawMax));
            setBtn(BBtn_A, bb.button_a);
            continue;
        }

        setAxis(Axis_AccelX, NormSymmetric(snap.accel.g_x, kAccelMaxG));
        setAxis(Axis_AccelY, NormSymmetric(snap.accel.g_y, kAccelMaxG));
        setAxis(Axis_AccelZ, NormSymmetric(snap.accel.g_z, kAccelMaxG));

        // IR: all 4 tracked points are bridged, each as its own X/Y axis
        // pair (Axis_IR1X/Y .. Axis_IR4X/Y), in the same slot order the
        // Wiimote report delivers them (see IRState/WiimoteDecoder.cpp) -
        // there's no persistent identity across frames beyond that slot
        // index, so "dot 1" simply means "whatever is in ir[0] right now".
        // Each rests at center (0) when its slot isn't visible, rather than
        // snapping to a rail, so an empty/unmapped camera view doesn't read
        // as "full deflection".
        static constexpr int kIRAxisX[4] = {Axis_IR1X, Axis_IR2X, Axis_IR3X, Axis_IR4X};
        static constexpr int kIRAxisY[4] = {Axis_IR1Y, Axis_IR2Y, Axis_IR3Y, Axis_IR4Y};
        for (int i = 0; i < 4; ++i) {
            const auto &dot = snap.ir[i];
            setAxis(kIRAxisX[i], dot.visible ? (float(dot.x) / 1023.0f) * 2.f - 1.f : 0.f);
            setAxis(kIRAxisY[i], dot.visible ? (float(dot.y) / 767.0f)  * 2.f - 1.f : 0.f);
        }

        // Dot size (0-15), only meaningful while IR Extended mode is active
        // (WiimoteDevice::SetIRExtendedMode) - IRDot::size stays 0 the rest
        // of the time, same as any other at-rest reading. Uses the same
        // "trigger at rest = -1" bipolar convention as the Balance Board's
        // weight axes above (Norm01ToBipolar), since size is a magnitude
        // with no natural sign, not a centered stick axis: 0 -> -1 (rest/
        // not visible), 15 -> +1 (biggest blob the camera reports).
        static constexpr int kIRAxisSize[4] = {Axis_IR1Size, Axis_IR2Size, Axis_IR3Size, Axis_IR4Size};
        for (int i = 0; i < 4; ++i) {
            const auto &dot = snap.ir[i];
            setAxis(kIRAxisSize[i], dot.visible ? Norm01ToBipolar(float(dot.size), 0.f, 15.f) : -1.f);
        }

        // Nunchuk stick: 8-bit, documented center ~128, physical range
        // roughly 35-228 (WiiBrew) - +-100 as the working half-range keeps
        // the mapping usable without needing per-device calibration.
        setAxis(Axis_NunchukX, (float(snap.nunchuk.stick_x) - 128.f) / 100.f);
        setAxis(Axis_NunchukY, (float(snap.nunchuk.stick_y) - 128.f) / 100.f);

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

        // D-Pad as a hat (see WiimoteHat's comment) - bits are ORable
        // directly into SDL's own hat bitmask (SDL_HAT_UP/DOWN/LEFT/RIGHT),
        // so a diagonal reads as e.g. SDL_HAT_LEFTUP automatically without
        // needing to special-case it here.
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

} // namespace InputBridge::Wiimote
