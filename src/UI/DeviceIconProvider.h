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
    ImFont* xbox          = nullptr;
    ImFont* playstation   = nullptr;
    ImFont* nintendoSwitch = nullptr;
    ImFont* nintendoWii   = nullptr;
    ImFont* nintendoGamecube = nullptr;
    ImFont* keyboardMouse = nullptr;
    ImFont* steamDeck     = nullptr;
    ImFont* steamController = nullptr;
    ImFont* generic       = nullptr;

    /// Invalidate all pointers (call before atlas rebuild).
    void Reset() { *this = KenneyFonts{}; }

    /// Returns true when at least one font was loaded successfully.
    bool AnyLoaded() const
    {
        return xbox || playstation || nintendoSwitch || nintendoWii
            || nintendoGamecube || keyboardMouse || steamDeck
            || steamController || generic;
    }

    /// Singleton accessor — shared between FontManager and DeviceIconProvider.
    static KenneyFonts& Get();
};

// ---------------------------------------------------------------------------
// DeviceIconProvider
// ---------------------------------------------------------------------------
struct DeviceIcon
{
    ImFont*     font  = nullptr;  ///< Kenney font containing the glyph (may be null)
    const char* glyph = nullptr;  ///< UTF-8 encoded codepoint string   (may be null)

    /// Returns true when both fields are valid and the icon can be rendered.
    bool IsValid() const { return font != nullptr && glyph != nullptr; }
};

class DeviceIconProvider
{
public:
    /// Returns the best matching Kenney icon for the given device.
    /// If the Kenney fonts have not been loaded (e.g. TTF files absent) the
    /// returned DeviceIcon will have IsValid() == false — callers must check.
    static DeviceIcon GetIcon(const DeviceState& dev);

private:
    static DeviceIcon IconFromName(const std::string& lower);
};
