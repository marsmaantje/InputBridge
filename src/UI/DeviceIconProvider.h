#pragma once
// DeviceIconProvider.h
//
// Selects the appropriate Kenney icon font and glyph character for a device
// based on its reported name string.  The fonts themselves are loaded and
// owned by FontManager; this helper queries them via the KenneyFonts registry.
//
// Typical usage (inside an ImGui frame):
//
//   auto [font, glyph] = DeviceIconProvider::GetIcon(dev);
//   if (font) {
//       ImGui::PushFont(font);
//       ImGui::Text("%s", glyph);
//       ImGui::PopFont();
//       ImGui::SameLine();
//   }
//   ImGui::Text("%s", dev.name.c_str());

#include "imgui.h"

#include <cstddef>

#include <string>

struct DeviceState;

// ---------------------------------------------------------------------------
// KenneyFonts
//
// Thin registry that FontManager fills during RebuildFontAtlas() and that
// DeviceIconProvider reads.  All pointers become invalid after the next
// RebuildFontAtlas() call (ImGui invalidates all ImFont* on atlas rebuild),
// so FontManager must call Reset() before clearing the atlas and re-populate
// afterwards.
// ---------------------------------------------------------------------------
struct KenneyFonts
{
    ImFont* xbox             = nullptr;
    ImFont* playstation      = nullptr;
    ImFont* nintendoSwitch   = nullptr;
    ImFont* nintendoWii      = nullptr;
    ImFont* nintendoGamecube = nullptr;
    ImFont* keyboardMouse    = nullptr;
    ImFont* steamDeck        = nullptr;
    ImFont* steamController  = nullptr;
    ImFont* generic          = nullptr;

    /// Invalidate all pointers (call before atlas rebuild).
    void Reset() { *this = KenneyFonts{}; }

    /// Returns true when at least one font was loaded successfully.
    bool AnyLoaded() const
    {
        return xbox || playstation || nintendoSwitch || nintendoWii
            || nintendoGamecube || keyboardMouse || steamDeck
            || steamController || generic;
    }

    /// Singleton accessor - shared between FontManager and DeviceIconProvider.
    static KenneyFonts& Get();
};

// ---------------------------------------------------------------------------
// DeviceIconProvider
// ---------------------------------------------------------------------------
struct DeviceIcon
{
    ImFont*  font         = nullptr;  ///< Kenney font containing the glyph (may be null)
    char     glyphBuf[4]  = {};       ///< UTF-8 encoded codepoint bytes, owned by this instance (NUL-terminated)
    ImWchar  codepoint    = 0;        ///< Unicode codepoint - used to look up glyph metrics via ImFontBaked::FindGlyph()

    DeviceIcon() = default;

    /// Builds from a font + compile-time UTF-8 string literal (from the
    /// paired KENNEY_<NAME> / KENNEY_<NAME>_CP macros) + codepoint, e.g.
    ///   return { fonts.generic, KENNEY_GENERIC_JOYSTICK, KENNEY_GENERIC_JOYSTICK_CP };
    /// utf8 is copied into glyphBuf (max 4 bytes incl. NUL) so this instance
    /// owns its bytes independently, same as the MakeIcon() path below.
    DeviceIcon(ImFont* f, const char* utf8, ImWchar cp) : font(f), codepoint(cp)
    {
        if (utf8) {
            std::size_t i = 0;
            for (; i < 3 && utf8[i] != '\0'; ++i) glyphBuf[i] = utf8[i];
            glyphBuf[i] = '\0';
        }
    }

    /// UTF-8 encoded codepoint string for rendering (e.g. via ImGui::Text/AddText).
    /// Points at this instance's own glyphBuf - safe to copy the struct by
    /// value (unlike a shared/static buffer, each copy keeps its own bytes).
    const char* glyph() const { return codepoint != 0 ? glyphBuf : nullptr; }

    /// Returns true when all fields are valid and the icon can be rendered.
    bool IsValid() const { return font != nullptr && codepoint != 0 && glyphBuf[0] != '\0'; }
};

class DeviceIconProvider
{
public:
    /// Returns the best matching Kenney icon for the given device.
    /// If the Kenney fonts have not been loaded (e.g. TTF files absent) the
    /// returned DeviceIcon will have IsValid() == false - callers must check.
    static DeviceIcon GetIcon(const DeviceState& dev);

private:
    static DeviceIcon IconFromName(const std::string& lower);
};
