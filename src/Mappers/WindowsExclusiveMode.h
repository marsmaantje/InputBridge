#pragma once

#ifdef _WIN32

#include "InputExclusiveModeImpl.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>


#include <set>
#include <string>
#include <vector>


// Windows backend using the HidHide kernel-mode filter driver.
//
// HidHide works at the HID bus level: it maintains a global per-device
// block-list (device instance paths) and a per-process allow-list.  When a
// device is on the block-list, every process that is NOT on the allow-list
// gets an ACCESS_DENIED when it tries to open the device node.
//
// InputBridge is automatically added to the allow-list on first use.
// Steam (steam.exe) can optionally be added so that Steam Input continues
// to work while the device is hidden from everything else.
//
// If HidHide is not installed the class gracefully reports IsAvailable()==false
// and all operations become no-ops.
class WindowsExclusiveMode : public InputExclusiveModeImpl {
  public:
    WindowsExclusiveMode();
    ~WindowsExclusiveMode() override;

    bool HideDevice(SDL_Joystick *joystick) override;
    bool UnhideDevice(SDL_Joystick *joystick) override;
    bool IsAvailable() const override;
    void SetSteamInputCompatible(bool enabled) override;

  private:
    // ── HidHide IOCTL interface ──────────────────────────────────────────

    // Device path of the HidHide control device.
    static constexpr const wchar_t *kHidHideDevice = L"\\\\.\\HidHide";

    // IOCTL codes  (FILE_DEVICE_UNKNOWN = 0x22, METHOD_BUFFERED = 0,
    //               FILE_READ_DATA | FILE_WRITE_DATA = 0x0003)
    // CTL_CODE(DeviceType, Function, Method, Access)
    static constexpr DWORD IOCTL_GET_BLACKLIST = 0x220404; // Function 0x101
    static constexpr DWORD IOCTL_SET_BLACKLIST = 0x220408; // Function 0x102
    static constexpr DWORD IOCTL_GET_WHITELIST = 0x220410; // Function 0x104
    static constexpr DWORD IOCTL_SET_WHITELIST = 0x220414; // Function 0x105
    static constexpr DWORD IOCTL_GET_ACTIVE = 0x220400;    // Function 0x100
    static constexpr DWORD IOCTL_SET_ACTIVE = 0x220404;    // same as GET

    // ── helpers ─────────────────────────────────────────────────────────

    // Open the HidHide control handle (caller must CloseHandle).
    HANDLE OpenControlDevice() const;

    // Read the current HidHide block-list / allow-list as sets of strings.
    std::set<std::wstring> GetBlockList() const;
    std::set<std::wstring> GetAllowList() const;

    // Write back a modified list.
    bool SetBlockList(const std::set<std::wstring> &list) const;
    bool SetAllowList(const std::set<std::wstring> &list) const;

    // Multi-string helpers (REG_MULTI_SZ style but for IOCTL buffers):
    //   buffer = L"entry0\0entry1\0\0"
    static std::set<std::wstring> ParseMultiString(const std::vector<wchar_t> &buf);
    static std::vector<wchar_t> BuildMultiString(const std::set<std::wstring> &entries);

    // Retrieve the HID instance path(s) for a joystick.
    // A single SDL device may expose one or more HID nodes.
    std::vector<std::wstring> GetInstancePaths(SDL_Joystick *joystick) const;

    // Return the fully-qualified executable path of the running process.
    static std::wstring GetOwnExePath();

    // Return the path to steam.exe if Steam is installed, otherwise empty.
    static std::wstring FindSteamExePath();

    bool EnsureWhitelisted(const std::wstring &own, const std::wstring &steam, bool steamCompat, bool &selfWhitelistedFlag);

    // True after we have successfully added ourselves to the allow-list.
    bool m_SelfWhitelisted = false;

    // Whether we should also keep steam.exe in the allow-list.
    bool m_SteamCompatible = true;

    // Cached exe paths (computed once).
    mutable std::wstring m_OwnExePath;
    mutable std::wstring m_SteamExePath;
};

#endif // _WIN32