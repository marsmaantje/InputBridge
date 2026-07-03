#include "App/Log.h"
#include "WindowsExclusiveMode.h"

#ifdef _WIN32

#include <SDL3/SDL.h>
#include <algorithm>
#include <cfgmgr32.h>
#include <cstring>
#include <devguid.h>
#include <regstr.h>
#include <setupapi.h>
#include <shlobj.h>

static constexpr const char* kTag = "ExclusiveMode";

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

// ─── CTL_CODE macro ──────────────────────────────────────────────────────────
// HidHide uses FILE_DEVICE_UNKNOWN (0x22), METHOD_BUFFERED (0),
// FILE_READ_DATA|FILE_WRITE_DATA (0x0003).
// CTL_CODE = (DevType<<16)|(Access<<14)|(Func<<2)|Method
//
// Verified against HidHide's open-source HidHideIoControl.h:
//   IOCTL_GET_WHITELIST  Function=0x101
//   IOCTL_SET_WHITELIST  Function=0x102
//   IOCTL_GET_BLACKLIST  Function=0x104  (device block-list)
//   IOCTL_SET_BLACKLIST  Function=0x105
//   IOCTL_GET_ACTIVE     Function=0x100
//   IOCTL_SET_ACTIVE     Function=0x100  (same code, write direction)

#ifndef CTL_CODE
#define CTL_CODE(DeviceType, Function, Method, Access) (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif

static constexpr DWORD kIoctlGetWhitelist = CTL_CODE(0x22, 0x101, 0 /*METHOD_BUFFERED*/, 0x0003);
static constexpr DWORD kIoctlSetWhitelist = CTL_CODE(0x22, 0x102, 0, 0x0003);
static constexpr DWORD kIoctlGetBlacklist = CTL_CODE(0x22, 0x104, 0, 0x0003);
static constexpr DWORD kIoctlSetBlacklist = CTL_CODE(0x22, 0x105, 0, 0x0003);
static constexpr DWORD kIoctlGetActive = CTL_CODE(0x22, 0x100, 0, 0x0003);
static constexpr DWORD kIoctlSetActive = CTL_CODE(0x22, 0x100, 0, 0x0003);

// ─── Constructor / destructor ─────────────────────────────────────────────────

WindowsExclusiveMode::WindowsExclusiveMode() {
    m_OwnExePath = GetOwnExePath();
    m_SteamExePath = FindSteamExePath();
}

WindowsExclusiveMode::~WindowsExclusiveMode() {
    // Nothing to release - HidHide state persists in the driver until we
    // explicitly remove entries, which we do in UnhideDevice().
}

// ─── IsAvailable ─────────────────────────────────────────────────────────────

bool WindowsExclusiveMode::IsAvailable() const {
    HANDLE h = OpenControlDevice();
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

// ─── SetSteamInputCompatible ──────────────────────────────────────────────────

void WindowsExclusiveMode::SetSteamInputCompatible(bool enabled) {
    m_SteamCompatible = enabled;

    // Immediately reflect in the allow-list if we're already whitelisted.
    if (!m_SelfWhitelisted)
        return;

    auto wl = GetAllowList();

    if (enabled && !m_SteamExePath.empty()) {
        wl.insert(m_SteamExePath);
    } else if (!enabled && !m_SteamExePath.empty()) {
        wl.erase(m_SteamExePath);
    }

    SetAllowList(wl);
}

// ─── EnsureSelfWhitelisted (internal helper) ──────────────────────────────────

bool WindowsExclusiveMode::EnsureWhitelisted(const std::wstring &own, const std::wstring &steam, bool steamCompat, bool &selfWhitelistedFlag) {
    // Avoid repeated work.
    if (selfWhitelistedFlag)
        return true;

    auto wl = GetAllowList();

    bool changed = false;
    if (!own.empty() && wl.find(own) == wl.end()) {
        wl.insert(own);
        changed = true;
    }
    if (steamCompat && !steam.empty() && wl.find(steam) == wl.end()) {
        wl.insert(steam);
        changed = true;
    }

    if (changed && !SetAllowList(wl)) {
        LOG_ERROR(kTag, "HidHide: failed to add InputBridge to the allow-list.");
        return false;
    }

    selfWhitelistedFlag = true;
    LOG_INFO(kTag, "HidHide: InputBridge added to allow-list.");
    return true;
}

// ─── HideDevice ───────────────────────────────────────────────────────────────

bool WindowsExclusiveMode::HideDevice(SDL_Joystick *joystick) {
    if (!IsAvailable())
        return false;

    // Make sure we are in the allow-list before hiding anything.
    if (!EnsureWhitelisted(m_OwnExePath, m_SteamExePath, m_SteamCompatible, m_SelfWhitelisted))
        return false;

    auto paths = GetInstancePaths(joystick);
    if (paths.empty()) {
        LOG_INFO(kTag, "HidHide: could not determine instance path for '%s'.", SDL_GetJoystickName(joystick));
        return false;
    }

    auto bl = GetBlockList();
    bool changed = false;
    for (auto &p : paths) {
        if (bl.find(p) == bl.end()) {
            bl.insert(p);
            changed = true;
            LOG_INFO(kTag, "HidHide: hiding device path '%ls'.", p.c_str());
        }
    }

    if (!changed)
        return true; // already hidden

    if (!SetBlockList(bl)) {
        LOG_ERROR(kTag, "HidHide: failed to write block-list.");
        return false;
    }

    // Make sure HidHide filtering is active.
    HANDLE h = OpenControlDevice();
    if (h != INVALID_HANDLE_VALUE) {
        BOOL active = TRUE;
        DWORD returned = 0;
        DeviceIoControl(h, kIoctlSetActive, &active, sizeof(active), nullptr, 0, &returned, nullptr);
        CloseHandle(h);
    }

    LOG_INFO(kTag, "HidHide: '%s' is now hidden.", SDL_GetJoystickName(joystick));
    return true;
}

// ─── UnhideDevice ─────────────────────────────────────────────────────────────

bool WindowsExclusiveMode::UnhideDevice(SDL_Joystick *joystick) {
    if (!IsAvailable())
        return false;

    auto paths = GetInstancePaths(joystick);
    if (paths.empty())
        return false;

    auto bl = GetBlockList();
    bool changed = false;
    for (auto &p : paths) {
        auto it = bl.find(p);
        if (it != bl.end()) {
            bl.erase(it);
            changed = true;
            LOG_INFO(kTag, "HidHide: unhiding device path '%ls'.", p.c_str());
        }
    }

    if (!changed)
        return true;

    if (!SetBlockList(bl)) {
        LOG_ERROR(kTag, "HidHide: failed to write block-list on unhide.");
        return false;
    }

    LOG_INFO(kTag, "HidHide: '%s' is now visible to all applications.", SDL_GetJoystickName(joystick));
    return true;
}

// ─── IOCTL helpers ────────────────────────────────────────────────────────────

HANDLE WindowsExclusiveMode::OpenControlDevice() const {
    HANDLE h = CreateFileW(kHidHideDevice, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        // Driver not installed - this is expected on systems without HidHide.
        LOG_WARN(kTag, "HidHide: control device not found (driver not installed?).");
    }
    return h;
}

// Generic IOCTL query that returns a multi-string buffer.
static std::vector<wchar_t> IoctlGetMultiString(HANDLE h, DWORD code) {
    // First call to determine required size.
    DWORD returned = 0;
    DeviceIoControl(h, code, nullptr, 0, nullptr, 0, &returned, nullptr);

    if (returned == 0)
        return {};

    std::vector<wchar_t> buf(returned / sizeof(wchar_t) + 2, L'\0');
    if (!DeviceIoControl(h, code, nullptr, 0, buf.data(), static_cast<DWORD>(buf.size() * sizeof(wchar_t)), &returned, nullptr))
        return {};

    return buf;
}

std::set<std::wstring> WindowsExclusiveMode::GetBlockList() const {
    HANDLE h = OpenControlDevice();
    if (h == INVALID_HANDLE_VALUE)
        return {};
    auto buf = IoctlGetMultiString(h, kIoctlGetBlacklist);
    CloseHandle(h);
    return ParseMultiString(buf);
}

std::set<std::wstring> WindowsExclusiveMode::GetAllowList() const {
    HANDLE h = OpenControlDevice();
    if (h == INVALID_HANDLE_VALUE)
        return {};
    auto buf = IoctlGetMultiString(h, kIoctlGetWhitelist);
    CloseHandle(h);
    return ParseMultiString(buf);
}

bool WindowsExclusiveMode::SetBlockList(const std::set<std::wstring> &list) const {
    HANDLE h = OpenControlDevice();
    if (h == INVALID_HANDLE_VALUE)
        return false;
    auto buf = BuildMultiString(list);
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, kIoctlSetBlacklist, buf.data(), static_cast<DWORD>(buf.size() * sizeof(wchar_t)), nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}

bool WindowsExclusiveMode::SetAllowList(const std::set<std::wstring> &list) const {
    HANDLE h = OpenControlDevice();
    if (h == INVALID_HANDLE_VALUE)
        return false;
    auto buf = BuildMultiString(list);
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, kIoctlSetWhitelist, buf.data(), static_cast<DWORD>(buf.size() * sizeof(wchar_t)), nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}

// ─── Multi-string parsing ─────────────────────────────────────────────────────

std::set<std::wstring> WindowsExclusiveMode::ParseMultiString(const std::vector<wchar_t> &buf) {
    std::set<std::wstring> result;
    if (buf.empty())
        return result;

    const wchar_t *p = buf.data();
    const wchar_t *end = p + buf.size();

    while (p < end && *p != L'\0') {
        std::wstring entry(p);
        if (!entry.empty())
            result.insert(entry);
        p += entry.size() + 1;
    }
    return result;
}

std::vector<wchar_t> WindowsExclusiveMode::BuildMultiString(const std::set<std::wstring> &entries) {
    std::vector<wchar_t> buf;
    for (const auto &s : entries) {
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back(L'\0');
    }
    buf.push_back(L'\0'); // double-null terminator
    return buf;
}

// ─── GetInstancePaths ─────────────────────────────────────────────────────────
//
// HidHide identifies devices by their "device instance path" (the string you
// see in Device Manager under "Details → Device instance path"), e.g.:
//   HID\VID_054C&PID_0CE6&IG_00\3&...
//
// SDL gives us the vendor/product IDs via SDL_GetJoystickVendor /
// SDL_GetJoystickProduct.  We use SetupDI to walk all HID devices and find
// the matching instance path(s).

std::vector<std::wstring> WindowsExclusiveMode::GetInstancePaths(SDL_Joystick *joystick) const {
    std::vector<std::wstring> result;

    Uint16 vid = SDL_GetJoystickVendor(joystick);
    Uint16 pid = SDL_GetJoystickProduct(joystick);

    // Build the hardware-ID prefix we expect: HID\VID_XXXX&PID_XXXX
    wchar_t hwIdPrefix[64];
    swprintf_s(hwIdPrefix, L"HID\\VID_%04X&PID_%04X", (unsigned)vid, (unsigned)pid);
    // Lowercase to match what SetupDi returns on some systems.
    std::wstring prefix(hwIdPrefix);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::towlower);

    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, L"HID", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);

    if (devInfo == INVALID_HANDLE_VALUE)
        return result;

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {
        wchar_t instancePath[MAX_PATH]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &devData, instancePath, MAX_PATH, nullptr))
            continue;

        std::wstring ip(instancePath);
        std::wstring ipLow = ip;
        std::transform(ipLow.begin(), ipLow.end(), ipLow.begin(), ::towlower);

        if (ipLow.find(prefix) == 0) {
            result.push_back(ip);
            LOG_INFO(kTag, "HidHide: matched instance path '%ls'.", ip.c_str());
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

// ─── GetOwnExePath ────────────────────────────────────────────────────────────

std::wstring WindowsExclusiveMode::GetOwnExePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

// ─── FindSteamExePath ─────────────────────────────────────────────────────────

std::wstring WindowsExclusiveMode::FindSteamExePath() {
    // Check the registry key set by the Steam installer.
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", 0, KEY_READ, &hKey);
    }

    if (!hKey)
        return {};

    wchar_t steamPath[MAX_PATH]{};
    DWORD size = sizeof(steamPath);
    DWORD type = REG_SZ;
    if (RegQueryValueExW(hKey, L"InstallPath", nullptr, &type, reinterpret_cast<LPBYTE>(steamPath), &size) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        std::wstring path(steamPath);
        if (!path.empty() && path.back() != L'\\')
            path += L'\\';
        path += L"steam.exe";
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return path;
    }

    RegCloseKey(hKey);
    return {};
}

#endif // _WIN32
