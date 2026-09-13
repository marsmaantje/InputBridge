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
// Each return carries { font, UTF-8 string, codepoint } - the string and
// codepoint always come from the paired macros in KenneyIcons.h so they
// can never drift out of sync.
// ---------------------------------------------------------------------------

DeviceIcon DeviceIconProvider::IconFromName(const std::string& lower)
{
    KenneyFonts& fonts = KenneyFonts::Get();

    // -- Steam Deck ---------------------------------------------------------
    if (lower.find("steam deck") != std::string::npos
        || lower.find("steamdeck") != std::string::npos)
    {
        return { fonts.steamDeck, KENNEY_STEAMDECK_CONTROLLER, KENNEY_STEAMDECK_CONTROLLER_CP };
    }

    // -- Steam Controller ---------------------------------------------------
    // Both glyph variants live in the same TTF; only the codepoint differs.
    //   KENNEY_STEAM_CONTROLLER     (U+E020) - original oval-body design (V1 / D0G)
    //   KENNEY_STEAM_CONTROLLER_NEW (U+E021) - redesigned rectangular body (V2 / HEADCRAB)
    //
    // SDL reports both generations with the same "Steam Controller" name string.
    if (lower.find("steam controller") != std::string::npos
        || lower.find("steam ctrl") != std::string::npos)
    {
        // "steam controller v2" → canonical name set by DeviceFactory for V2 (HEADCRAB) hardware.
        if (lower.find("v2") != std::string::npos)
            return { fonts.steamController, KENNEY_STEAM_CONTROLLER_NEW, KENNEY_STEAM_CONTROLLER_NEW_CP };

        // "steam controller v1", or any plain "steam controller" fallback → original design.
        return { fonts.steamController, KENNEY_STEAM_CONTROLLER, KENNEY_STEAM_CONTROLLER_CP };
    }

    // -- Xbox ---------------------------------------------------------------
    if (lower.find("xbox") != std::string::npos
        || lower.find("x-box") != std::string::npos
        || lower.find("xinput") != std::string::npos)
    {
        if (lower.find("360") != std::string::npos)
            return { fonts.xbox, KENNEY_XBOX_CONTROLLER_360,    KENNEY_XBOX_CONTROLLER_360_CP };
        return     { fonts.xbox, KENNEY_XBOX_CONTROLLER_SERIES, KENNEY_XBOX_CONTROLLER_SERIES_CP };
    }

    // -- PlayStation / DualSense / DualShock --------------------------------
    if (lower.find("playstation") != std::string::npos
        || lower.find("dualshock") != std::string::npos
        || lower.find("dualsense") != std::string::npos
        || lower.find("ps3") != std::string::npos
        || lower.find("ps4") != std::string::npos
        || lower.find("ps5") != std::string::npos)
    {
        if (lower.find("dualsense") != std::string::npos || lower.find("ps5") != std::string::npos)
            return { fonts.playstation, KENNEY_PS_CONTROLLER_PS5, KENNEY_PS_CONTROLLER_PS5_CP };
        return     { fonts.playstation, KENNEY_PS_CONTROLLER_PS4, KENNEY_PS_CONTROLLER_PS4_CP };
    }

    // -- Nintendo Switch (Joy-Con, Pro Controller) --------------------------
    if (lower.find("nintendo switch") != std::string::npos
        || lower.find("switch pro") != std::string::npos
        || lower.find("joy-con") != std::string::npos
        || lower.find("joycon") != std::string::npos
        || lower.find("pro controller") != std::string::npos)
    {
        if (lower.find("pro") != std::string::npos)
            return { fonts.nintendoSwitch, KENNEY_SWITCH_CONTROLLER_PRO, KENNEY_SWITCH_CONTROLLER_PRO_CP };
        return     { fonts.nintendoSwitch, KENNEY_SWITCH_CONTROLLER,     KENNEY_SWITCH_CONTROLLER_CP };
    }

    // -- Nintendo Wii / Wiimote ---------------------------------------------
    // Includes WiimoteVirtualBridge's own bridge device names ("Wii
    // Controller (Mapped Inputs)" / "Wii Balance Board (Mapped Inputs)") -
    // see Devices/Wiimote/WiimoteVirtualBridge.h. Those are what actually
    // show up in the device list now (real Wiimotes/Balance Boards are
    // filtered out of DeviceManager::GetDevices() entirely - see
    // DeviceManager::HandleDeviceAdded's Wiimote-family filter), so without
    // this branch matching them, both fell through to the generic
    // "Nintendo" -> Switch icon below instead of a Wii one.
    if (lower.find("wiimote") != std::string::npos
        || lower.find("wii remote") != std::string::npos
        || lower.find("nintendo wii") != std::string::npos
        || lower.find("wii u") != std::string::npos
        || lower.find("wii controller") != std::string::npos
        || lower.find("wii balance board") != std::string::npos)
    {
        // The Balance Board is flat and wide rather than held upright -
        // wii_controller_horizontal is the closest fit in this font (there
        // is no dedicated Balance Board glyph).
        if (lower.find("balance board") != std::string::npos)
            return { fonts.nintendoWii, KENNEY_WII_CONTROLLER_HORIZONTAL, KENNEY_WII_CONTROLLER_HORIZONTAL_CP };
        return { fonts.nintendoWii, KENNEY_WII_CONTROLLER, KENNEY_WII_CONTROLLER_CP };
    }

    // -- Nintendo GameCube --------------------------------------------------
    if (lower.find("gamecube") != std::string::npos
        || lower.find("game cube") != std::string::npos
        || lower.find("ngc") != std::string::npos)
    {
        return { fonts.nintendoGamecube, KENNEY_GC_CONTROLLER, KENNEY_GC_CONTROLLER_CP };
    }

    // -- Generic "Nintendo" fall-through → Switch icon ---------------------
    if (lower.find("nintendo") != std::string::npos)
    {
        return { fonts.nintendoSwitch, KENNEY_SWITCH_CONTROLLER, KENNEY_SWITCH_CONTROLLER_CP };
    }

    // -- Keyboard -----------------------------------------------------------
    if (lower.find("keyboard") != std::string::npos)
    {
        return { fonts.keyboardMouse, KENNEY_KBM_KEYBOARD, KENNEY_KBM_KEYBOARD_CP };
    }

    // -- Mouse --------------------------------------------------------------
    if (lower.find("mouse") != std::string::npos
        || lower.find("trackpad") != std::string::npos
        || lower.find("touchpad") != std::string::npos)
    {
        return { fonts.keyboardMouse, KENNEY_KBM_MOUSE, KENNEY_KBM_MOUSE_CP };
    }

    // -- Generic joystick / HOTAS / flight stick / steering wheel ----------
    return { fonts.generic, KENNEY_GENERIC_JOYSTICK, KENNEY_GENERIC_JOYSTICK_CP };
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
