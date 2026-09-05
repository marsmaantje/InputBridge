// src/Devices/Wiimote/WiimoteVirtualBridge.h
//
// Bridges WiimoteDevice's decoded state into a virtual SDL_Joystick, so
// every Wiimote/Balance Board becomes mappable through InputMapper with
// zero changes there - the mapping data model addresses devices via
// SDL_JoystickID + axis/button index (see MappingTypes.h), not our
// WiimoteDevice type. Same SDL_AttachVirtualJoystick +
// SDL_SetJoystickVirtual{Axis,Button} pattern as VirtualDeviceManager, just
// with a Wiimote-specific layout instead of a generic gamepad/wheel preset.
#pragma once
#include "WiimoteDevice.h"
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <memory>

namespace InputBridge::Wiimote {

// -- Bridge joystick axis/button layout --------------------------------------
// Single source of truth for the virtual joystick's channel layout, shared
// between WiimoteVirtualBridge.cpp (pushes values here) and
// InputLabelProvider.cpp (needs matching names for the "Raw Inputs" tab).
// Order/count must stay in sync with PushAllStates()'s setAxis/setBtn calls.
//
// IMPORTANT: MappingProfileStore persists bindings by raw axis index, not
// enum name, so an index already in use must never shift - always APPEND
// new axes at the end, never insert. (Axis_IRX/Y were renamed to
// Axis_IR1X/Y in place, same slot, to preserve old saved profiles.)
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

// The main D-Pad is a hat (not 4 buttons) so it maps/displays like a
// regular gamepad's D-Pad - InputMapper already understands hats
// generically (MappingTypes.h). The Classic Controller's D-Pad is a
// separate physical pad and stays as plain buttons below.
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

// Same append-only rule as WiimoteAxis - BAxis_Battery was appended after
// the original 7-axis layout rather than inserted inline.
constexpr int kBalanceNumAxes = 8;
enum BalanceAxis {
    BAxis_TopLeft = 0, BAxis_TopRight, BAxis_BottomLeft, BAxis_BottomRight,
    BAxis_Total, BAxis_CoGX, BAxis_CoGY,
    BAxis_Battery,
};
constexpr int kBalanceNumButtons = 1;
enum BalanceButton { BBtn_A = 0 };

// Device names Attach() gives the two bridge joystick kinds -
// InputLabelProvider exact-matches on these to pick these tables over
// SDL's gamepad binding walk.
constexpr const char *kWiimoteBridgeDeviceName       = "Wii Controller (Mapped Inputs)";
constexpr const char *kBalanceBoardBridgeDeviceName  = "Wii Balance Board (Mapped Inputs)";

// Human-readable names for the layouts above, in index order. Returns
// nullptr out of range (caller falls back to a numbered label).
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

    // Creates a bridge joystick for any new WiimoteDevice, and detaches
    // ones whose underlying device is gone (matched by hid_path). Call
    // once per frame - cheap no-op when nothing changed.
    void Sync(const std::vector<std::unique_ptr<WiimoteDevice>> &wiimotes);

    // Pushes each tracked WiimoteDevice's snapshot into its bridge
    // joystick's axes/buttons. Call once per frame, after Sync() and after
    // the WiimoteDevices are polled - same ordering as
    // VirtualDeviceManager::PushAllStates() vs. InputMapper::Update().
    void PushAllStates(const std::vector<std::unique_ptr<WiimoteDevice>> &wiimotes);

    // Detaches every bridge joystick. Call on shutdown, alongside
    // DeviceManager::CloseAllDevices().
    void RemoveAll();

    // Looks up the HID path backing a bridge joystick id, so callers with
    // only an SDL_JoystickID (e.g. OutputMapper's HapticTarget) can find
    // the real WiimoteDevice via DeviceManager::GetWiimotes() - the bridge
    // joystick itself is virtual with no rumble motor of its own. Returns
    // nullptr if not a tracked bridge joystick. out_is_balance_board, if
    // set, flags a Balance Board (no rumble motor at all).
    const std::string *FindHidPathForJoystick(SDL_JoystickID joystick_id, bool *out_is_balance_board = nullptr) const;

    // All currently-attached bridge joystick ids, so callers enumerating
    // mappable devices (e.g. MappingProfileStore::HandleDeviceConnectionChange())
    // can include Wiimotes alongside DeviceManager::GetDevices() - Wiimotes
    // are never in DeviceManager::m_Devices, so without this a saved
    // profile referencing a Wiimote GUID can't remap to a live instance_id.
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
