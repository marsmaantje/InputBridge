#include "DeviceIconProvider.h"

#include "Devices/DeviceState.h"
#include "UI/KenneyIcons.h"

#include <algorithm>
#include <cctype>
#include <string>

// ---------------------------------------------------------------------------
// KenneyFonts singleton
// ---------------------------------------------------------------------------

KenneyFonts& KenneyFonts::Get()
{
    static KenneyFonts instance;
    return instance;
}

// ---------------------------------------------------------------------------
// DeviceIconProvider::IconFromName
//
// Matches well-known substrings in the lowercased device name.  The order of
// checks matters: more specific brands/models are tested before generic ones.
// Each return carries the ImFont*, the UTF-8 glyph string AND the raw Unicode
// codepoint so DevicePanel can call ImFontBaked::FindGlyph() to read the
// exact pixel bounds of the glyph and scale it correctly.
// ---------------------------------------------------------------------------

DeviceIcon DeviceIconProvider::IconFromName(const std::string& lower)
{
    KenneyFonts& fonts = KenneyFonts::Get();

    // ── Steam Deck ─────────────────────────────────────────────────────────
    if (lower.find("steam deck") != std::string::npos
        || lower.find("steamdeck") != std::string::npos)
    {
        return { fonts.steamDeck, KENNEY_STEAMDECK_CONTROLLER, 0xE000 };
    }

    // ── Steam Controller ───────────────────────────────────────────────────
    if (lower.find("steam controller") != std::string::npos
        || lower.find("steam ctrl") != std::string::npos)
    {
        return { fonts.steamController, KENNEY_STEAM_CONTROLLER, 0xE020 };
    }

    // ── Xbox ───────────────────────────────────────────────────────────────
    if (lower.find("xbox") != std::string::npos
        || lower.find("x-box") != std::string::npos
        || lower.find("xinput") != std::string::npos)
    {
        if (lower.find("360") != std::string::npos)
            return { fonts.xbox, KENNEY_XBOX_CONTROLLER_360,    0xE000 };
        return     { fonts.xbox, KENNEY_XBOX_CONTROLLER_SERIES, 0xE003 };
    }

    // ── PlayStation / DualSense / DualShock ────────────────────────────────
    if (lower.find("playstation") != std::string::npos
        || lower.find("dualshock") != std::string::npos
        || lower.find("dualsense") != std::string::npos
        || lower.find("ps3") != std::string::npos
        || lower.find("ps4") != std::string::npos
        || lower.find("ps5") != std::string::npos)
    {
        if (lower.find("dualsense") != std::string::npos || lower.find("ps5") != std::string::npos)
            return { fonts.playstation, KENNEY_PS_CONTROLLER_PS5, 0xE004 };
        return     { fonts.playstation, KENNEY_PS_CONTROLLER_PS4, 0xE003 };
    }

    // ── Nintendo Switch (Joy-Con, Pro Controller) ──────────────────────────
    if (lower.find("nintendo switch") != std::string::npos
        || lower.find("switch pro") != std::string::npos
        || lower.find("joy-con") != std::string::npos
        || lower.find("joycon") != std::string::npos
        || lower.find("pro controller") != std::string::npos)
    {
        if (lower.find("pro") != std::string::npos)
            return { fonts.nintendoSwitch, KENNEY_SWITCH_CONTROLLER_PRO, 0xE003 };
        return     { fonts.nintendoSwitch, KENNEY_SWITCH_CONTROLLER,     0xE000 };
    }

    // ── Nintendo Wii / Wiimote ─────────────────────────────────────────────
    if (lower.find("wiimote") != std::string::npos
        || lower.find("wii remote") != std::string::npos
        || lower.find("nintendo wii") != std::string::npos
        || lower.find("wii u") != std::string::npos)
    {
        return { fonts.nintendoWii, KENNEY_WII_CONTROLLER, 0xE022 };
    }

    // ── Nintendo GameCube ──────────────────────────────────────────────────
    if (lower.find("gamecube") != std::string::npos
        || lower.find("game cube") != std::string::npos
        || lower.find("ngc") != std::string::npos)
    {
        return { fonts.nintendoGamecube, KENNEY_GC_CONTROLLER, 0xE014 };
    }

    // ── Generic "Nintendo" fall-through → Switch icon ─────────────────────
    if (lower.find("nintendo") != std::string::npos)
    {
        return { fonts.nintendoSwitch, KENNEY_SWITCH_CONTROLLER, 0xE000 };
    }

    // ── Keyboard ───────────────────────────────────────────────────────────
    if (lower.find("keyboard") != std::string::npos)
    {
        return { fonts.keyboardMouse, KENNEY_KBM_KEYBOARD, 0xE000 };
    }

    // ── Mouse ──────────────────────────────────────────────────────────────
    if (lower.find("mouse") != std::string::npos
        || lower.find("trackpad") != std::string::npos
        || lower.find("touchpad") != std::string::npos)
    {
        return { fonts.keyboardMouse, KENNEY_KBM_MOUSE, 0xE0E9 };
    }

    // ── Generic joystick / HOTAS / flight stick / steering wheel ──────────
    return { fonts.generic, KENNEY_GENERIC_JOYSTICK, 0xE013 };
}

// ---------------------------------------------------------------------------
// DeviceIconProvider::GetIcon
// ---------------------------------------------------------------------------

DeviceIcon DeviceIconProvider::GetIcon(const DeviceState& dev)
{
    if (!KenneyFonts::Get().AnyLoaded())
        return {};

    // Lowercase the device name once for all comparisons.
    std::string lower = dev.name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    DeviceIcon icon = IconFromName(lower);

    // Safety: if the chosen font wasn't loaded (TTF absent), return nothing.
    if (!icon.IsValid())
        return {};

    return icon;
}
