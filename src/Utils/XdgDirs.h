#pragma once

// ── XDG Base Directory helpers ────────────────────────────────────────────────
//
// On Linux (including AppImage and Flatpak), user files must follow the XDG
// Base Directory Specification so the application behaves like a well-mannered
// native app and supports AppImage Portable Mode out of the box.
//
//   Config  → $XDG_CONFIG_HOME/InputBridge/   (fallback: ~/.config/InputBridge/)
//   Data    → $XDG_DATA_HOME/InputBridge/     (fallback: ~/.local/share/InputBridge/)
//
// On Windows and macOS the helpers fall back to SDL_GetPrefPath so behaviour on
// those platforms is unchanged.
//
// Portable Mode note: AppImage remaps $HOME (and therefore $XDG_CONFIG_HOME /
// $XDG_DATA_HOME) when the user places a .home or .config directory next to the
// .AppImage file.  Because we read these env-vars at runtime rather than
// hardcoding absolute paths, Portable Mode works without any extra code here.

#include <string>
#include <filesystem>
#include <cstdlib>        // std::getenv
#include <SDL3/SDL_filesystem.h>

namespace XdgDirs {

namespace detail {

// Returns path with a guaranteed trailing '/'.
inline std::string withSlash(std::string p) {
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

// Read an XDG env-var; return the fallback if it is unset or empty.
inline std::string xdgEnv(const char* var, const char* fallbackRelative) {
    const char* val = std::getenv(var);
    if (val && val[0] != '\0')
        return withSlash(std::string(val));

    // Build fallback from $HOME.
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0')
        return withSlash(std::string(home)) + fallbackRelative + "/";

    return {};
}

} // namespace detail

/// Returns the XDG config directory for InputBridge, with a trailing '/'.
/// On Linux: $XDG_CONFIG_HOME/InputBridge/  (fallback ~/.config/InputBridge/)
/// On other platforms: SDL_GetPrefPath fallback.
inline std::string configDir() {
#if defined(__linux__) || defined(__FreeBSD__)
    std::string base = detail::xdgEnv("XDG_CONFIG_HOME", ".config");
    if (!base.empty()) {
        std::string dir = base + "InputBridge/";
        std::filesystem::create_directories(dir);
        return dir;
    }
#endif
    // Non-Linux fallback (Windows, macOS) — use SDL.
    const char* p = SDL_GetPrefPath("InputBridge", "config");
    if (p) {
        std::string dir(p);
        SDL_free(const_cast<char*>(p));
        return detail::withSlash(dir);
    }
    return "./";
}

/// Returns the XDG data directory for InputBridge, with a trailing '/'.
/// On Linux: $XDG_DATA_HOME/InputBridge/  (fallback ~/.local/share/InputBridge/)
/// On other platforms: SDL_GetPrefPath fallback.
inline std::string dataDir() {
#if defined(__linux__) || defined(__FreeBSD__)
    std::string base = detail::xdgEnv("XDG_DATA_HOME", ".local/share");
    if (!base.empty()) {
        std::string dir = base + "InputBridge/";
        std::filesystem::create_directories(dir);
        return dir;
    }
#endif
    // Non-Linux fallback (Windows, macOS) — use SDL.
    const char* p = SDL_GetPrefPath("InputBridge", "InputBridge");
    if (p) {
        std::string dir(p);
        SDL_free(const_cast<char*>(p));
        return detail::withSlash(dir);
    }
    return "./";
}

} // namespace XdgDirs