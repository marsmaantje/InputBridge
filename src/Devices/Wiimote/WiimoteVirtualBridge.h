// src/Devices/Wiimote/WiimoteVirtualBridge.h
//
// Bridges WiimoteDevice's decoded state into a real (virtual) SDL_Joystick,
// so every Wiimote/Balance Board becomes mappable through InputMapper with
// ZERO changes to InputMapper/InputMapperUI/InputBindingListener/
// OutputRuntimeUpdater/MappingProfileStore - all of those already work with
// any SDL_Joystick uniformly, since the entire mapping data model
// (InputSource, ButtonToDigitalMapping, etc, see MappingTypes.h) addresses
// devices via SDL_JoystickID + axis/button index, not our own WiimoteDevice
// type. Mirrors VirtualDeviceManager's SDL_AttachVirtualJoystick +
// SDL_SetJoystickVirtual{Axis,Button} pattern (see
// Devices/VirtualDeviceManager.cpp) - same mechanism, just with a
// Wiimote-specific axis/button layout instead of a generic gamepad/wheel/
// flight-stick preset.
#pragma once
#include "WiimoteDevice.h"
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <memory>

namespace InputBridge::Wiimote {

// -- Bridge joystick axis/button layout --------------------------------------
// Single source of truth for the virtual joystick's channel layout, shared
// between WiimoteVirtualBridge.cpp (which pushes values at these indices)
// and InputLabelProvider.cpp (which needs the matching names for the "Raw
// Inputs" tab - see WiimoteVirtualBridge.cpp's Attach() for why that tab
// can't just use SDL's gamepad binding tables here). Order and count MUST
// stay in sync with the setAxis/setBtn calls and the D-Pad hat push in
// PushAllStates() - there's no way to derive one from the other
// automatically since SDL only wants a count at attach time, so both
// consumers include this header rather than keeping their own copies.
// NOTE on ordering: MappingProfileStore persists bindings by raw axis
// index (see MappingProfileStore.h/.cpp), not by enum name, so existing
// saved profiles would silently rebind to the wrong physical input if an
// index already in use ever shifted. Axis_IRX/Axis_IRY (indices 3/4) are
// therefore kept in place as Axis_IR1X/Axis_IR1Y - just a rename, same
// slot, same values a saved profile already expects there - and each
// subsequent addition (the 3 extra dots, then the 4 dot-size axes, then
// battery) has been appended after the existing layout (indices 14-19,
// then 20-23, then 24) rather than inserted inline. Any future additions
// should do the same: append, don't insert. The Nunchuk accelerometer axes
// (indices 25-27) follow this same rule - appended after Axis_Battery
// rather than inserted next to Axis_NunchukX/Y, even though they're
// logically related to the Nunchuk stick axes above.
constexpr int kWiimoteNumAxes = 28;
enum WiimoteAxis {
    Axis_AccelX = 0, Axis_AccelY, Axis_AccelZ,
    Axis_IR1X, Axis_IR1Y, Axis_IR2X, Axis_IR2Y,
    Axis_IR3X, Axis_IR3Y, Axis_IR4X, Axis_IR4Y,
    Axis_IR1Size, Axis_IR2Size, Axis_IR3Size, Axis_IR4Size,
    Axis_Battery,
    Axis_NunchukX, Axis_NunchukY,
    Axis_NunchukAccelX, Axis_NunchukAccelY, Axis_NunchukAccelZ,
    Axis_MotionPlusYaw, Axis_MotionPlusPitch, Axis_MotionPlusRoll,
    Axis_ClassicLX, Axis_ClassicLY, Axis_ClassicRX, Axis_ClassicRY,
};

// The main D-Pad is exposed as a hat (not 4 separate buttons) so it maps
// and displays the same way a regular gamepad's D-Pad does - diagonal
// presses collapse to a single hat state instead of two simultaneous
// button events, and InputMapper/InputBindingListener already understand
// hats generically (see MappingTypes.h's hat_index/hat_mask), so no
// Wiimote-specific mapping code is needed. The Classic Controller
// extension's D-Pad is a separate physical pad on a separate device and
// stays as plain buttons (Btn_ClassicUp/Down/Left/Right below).
constexpr int kWiimoteNumHats = 1;
enum WiimoteHat { Hat_DPad = 0 };

constexpr int kWiimoteNumButtons = 21;
enum WiimoteButton {
    Btn_A = 0, Btn_B, Btn_One, Btn_Two, Btn_Plus, Btn_Minus, Btn_Home,
    Btn_NunchukC, Btn_NunchukZ,
    Btn_ClassicA, Btn_ClassicB, Btn_ClassicX, Btn_ClassicY,
    Btn_ClassicL, Btn_ClassicR, Btn_ClassicZL, Btn_ClassicZR,
    Btn_ClassicUp, Btn_ClassicDown, Btn_ClassicLeft, Btn_ClassicRight,
};

// Same "append, don't insert" rule as WiimoteAxis above applies here -
// BAxis_Battery is appended after the original 7-axis layout (index 7)
// rather than inserted inline.
constexpr int kBalanceNumAxes = 8;
enum BalanceAxis {
    BAxis_TopLeft = 0, BAxis_TopRight, BAxis_BottomLeft, BAxis_BottomRight,
    BAxis_Total, BAxis_CoGX, BAxis_CoGY,
    BAxis_Battery,
};
constexpr int kBalanceNumButtons = 1;
enum BalanceButton { BBtn_A = 0 };

// The exact device names Attach() gives the two bridge joystick kinds -
// InputLabelProvider matches on these (exact match, not substring) to
// decide whether to use the tables below instead of SDL's gamepad binding
// walk. Exposed here so both sides reference the same string constants
// rather than each hardcoding a copy that could drift.
constexpr const char *kWiimoteBridgeDeviceName       = "Wii Controller (Mapped Inputs)";
constexpr const char *kBalanceBoardBridgeDeviceName  = "Wii Balance Board (Mapped Inputs)";

// Human-readable names for the layouts above, in index order. Returns
// nullptr for an out-of-range index (caller should fall back to a numbered
// label in that case, same as any other unbound axis/button).
const char *WiimoteBridgeAxisName(int axis);
const char *WiimoteBridgeButtonName(int button);
const char *WiimoteBridgeHatName(int hat);
const char *BalanceBoardBridgeAxisName(int axis);
const char *BalanceBoardBridgeButtonName(int button);

class WiimoteVirtualBridge {
public:
    static WiimoteVirtualBridge &GetInstance();

    WiimoteVirtualBridge(const WiimoteVirtualBridge &) = delete;
    WiimoteVirtualBridge &operator=(const WiimoteVirtualBridge &) = delete;

    // Creates a bridge joystick for any WiimoteDevice not yet bridged, and
    // detaches bridge joysticks whose underlying WiimoteDevice is gone
    // (matched by hid_path). Call once per frame - cheap no-op when nothing
    // changed since the last call.
    void Sync(const std::vector<std::unique_ptr<WiimoteDevice>> &wiimotes);

    // Pushes each tracked WiimoteDevice's current snapshot into its bridge
    // joystick's axes/buttons so SDL_GetJoystickAxis/Button (and therefore
    // InputMapper) see current values. Call once per frame, after Sync()
    // and after the WiimoteDevices themselves have been polled for the
    // frame - same ordering VirtualDeviceManager::PushAllStates() uses
    // relative to InputMapper::Update().
    void PushAllStates(const std::vector<std::unique_ptr<WiimoteDevice>> &wiimotes);

    // Detaches every bridge joystick. Call on shutdown, alongside
    // DeviceManager::CloseAllDevices().
    void RemoveAll();

    // Looks up the HID path of the WiimoteDevice backing a given bridge
    // joystick id, so callers that only have an SDL_JoystickID (e.g.
    // OutputMapper's HapticTarget::instance_id) can find their way back to
    // the real WiimoteDevice via DeviceManager::GetWiimotes() - the bridge
    // joystick itself is virtual and has no rumble motor of its own (see
    // the file header comment). Returns nullptr if joystick_id isn't a
    // currently-tracked bridge joystick. out_is_balance_board, if non-null,
    // is set to whether the matched entry is a Balance Board (which has no
    // rumble motor at all, unlike a Wii Remote/Wii Remote Plus).
    const std::string *FindHidPathForJoystick(SDL_JoystickID joystick_id, bool *out_is_balance_board = nullptr) const;

    // Returns the SDL_JoystickID of every currently-attached bridge
    // joystick, so callers that need to enumerate all mappable devices
    // (e.g. MappingProfileStore::HandleDeviceConnectionChange(), which
    // rebuilds its GUID -> SDL_JoystickID lookup from scratch on every
    // profile activation / device connect-disconnect) can include Wiimotes
    // alongside DeviceManager::GetDevices(). Without this, saved profile
    // entries that reference a Wiimote GUID silently fail to remap to a
    // live instance_id, since Wiimotes are never present in
    // DeviceManager::m_Devices (see DeviceManager.h).
    std::vector<SDL_JoystickID> GetAllJoystickIds() const;

private:
    WiimoteVirtualBridge() = default;

    struct Entry {
        std::string hid_path;
        SDL_JoystickID joystick_id = 0;
        SDL_Joystick *joystick = nullptr;
        bool is_balance_board = false;
    };
    std::vector<Entry> m_Entries;

    Entry *Find(const std::string &hid_path);
    void Attach(const WiimoteDevice &dev);
    void Detach(Entry &entry);
};

} // namespace InputBridge::Wiimote
