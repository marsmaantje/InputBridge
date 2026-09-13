#include "AboutWindow.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace {
    // Case-insensitive strcmp. ImStricmp() lives in imgui_internal.h, which we
    // don't otherwise need, so we provide our own instead of including it.
    int CaseInsensitiveCompare(const char* a, const char* b) {
        while (*a && *b) {
            const int ca = std::tolower(static_cast<unsigned char>(*a));
            const int cb = std::tolower(static_cast<unsigned char>(*b));
            if (ca != cb) return ca - cb;
            ++a; ++b;
        }
        return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
    }
}

void AboutWindow::DrawContent() {
    // -- Title ----------------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
    ImGui::PushFont(NULL, ImGui::GetFontSize() * 1.4f); // Use PushFont with NULL to scale current font
    ImGui::Text("InputBridge");
    ImGui::PopFont(); // Pop the scaled font
    ImGui::PopStyleColor();
    // Get version number from top of CMakeLists
    ImGui::TextDisabled("v" INPUTBRIDGE_VERSION);

    ImGui::Spacing();

    // -- Description ---------------------------------------------------------
    if (ImGui::BeginTable("##description", 1)) {
        ImGui::TableNextColumn();
        ImGui::TextWrapped(
            "InputBridge is a tool for forwarding game controller inputs (steering "
            "wheels, gamepads, flight sticks and more) to external applications via OSC and WebSocket protocols.\r\n"
            "With the initial goal for having steering wheel (with force feedback) support in Resonite."
        );

        ImGui::Spacing();
        ImGui::TextWrapped(
            "The project was originally motivated by Water from the BackAlleyRacing group in "
            "Resonite, who needed help with fixing a previous minimal Windows prototype."
        );

        ImGui::Spacing();
        ImGui::TextWrapped(
            "At some point the feature creep started taking over and it may have gone a bit out of control ever since."
            "Hope you enjoy the tool."
        );

        ImGui::Spacing();
        ImGui::TextWrapped(
            "The Science Team members don't take any responsibility for the:\r\n"
            "bad coding style, lack of structure and potential loss of sanity while reading the code.\r\n"
            "It's handed out as is, deal with it :P"
        );
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -- The Science Team ----------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("The Science Team");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    struct Member { const char* name; const char* role; };
    constexpr Member members[] = {
        { "Elektrospy",  "Developer" },
        { "marsmaantje", "Developer" },
    };
    ImGui::Indent(8.0f);
    for (const auto& m : members) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::Text("%-16s", m.name);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m.role);
    }
    ImGui::Unindent(8.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("##testers", 2)) {
        ImGui::TableNextColumn();
        // -- Alpha Testers ----------------------------------------------------
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
        ImGui::Text("Alpha Testers");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        constexpr const char* testers_alpha[] = {
            "Beaned", "Dawky", "QueenHidi", "Snuhr", "TheMagician", "Water"
        };
        ImGui::Indent(8.0f);
        for (const char* t : testers_alpha) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::Text("%s", t);
        }
        ImGui::Unindent(8.0f);

        ImGui::TableNextColumn();
        // -- Beta Testers -----------------------------------------------------
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
        ImGui::Text("Beta Testers");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        constexpr const char* testers_beta[] = {
            "0x8081", "Decoy", "esnya", "GranpaVape", "Hayden", "LieutenantSparkles", "ShadowX", "SnubbleJr"
        };
        ImGui::Indent(8.0f);
        for (const char* t : testers_beta) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::Text("%s", t);
        }
        ImGui::Unindent(8.0f);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -- Libraries ------------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("Open-Source Libraries and Resources used");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    struct Lib { const char* name; const char* description; const char* license; };
    static constexpr std::array<Lib, 10> libs_source = {{
        { "SDL3",                 "Cross-platform hardware abstraction (input, audio, video)",  "zlib License"         },
        { "Dear ImGui",           "Immediate-mode graphical user interface library",            "MIT License"          },
        { "liblo",                "Lightweight OSC (Open Sound Control) implementation",        "LGPL 2.1"             },
        { "nlohmann/json",        "Single-header JSON parser and serialiser for C++",           "MIT License"          },
        { "uWebSockets",          "High-performance WebSocket server library",                  "Apache 2.0"           },
        { "uSockets",             "Low-level async networking (used by uWebSockets)",           "Apache 2.0"           },
        { "Google Fonts",         "Used for the Resonite and VRChat color themes",              "Apache 2.0 / OFL 1.1" },
        { "Font Awesome 6",       "Icon font used throughout the user interface",               "SIL OFL 1.1"          },
        { "Kenney Input Prompts", "Icon fonts for game controller and device glyphs",           "CC0 1.0"              },
        { "WiimoteLib",           "Wii Remote .Net library by Brian Peek. (Ported to C++)",     "MIT License"          },
    }};

    // Mutable, sortable copy of the library list (persists across frames).
    static std::array<Lib, libs_source.size()> libs = libs_source;

    if (ImGui::BeginTable("##libs", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_SortTristate)) {
        ImGui::TableSetupColumn("Library",     ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 1.8f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 4.0f);
        ImGui::TableSetupColumn("License",     ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();

        // Re-sort (alphabetically/numerically, case-insensitive) whenever the
        // user clicks a header and the sort specs become dirty.
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty || sort_specs->SpecsCount == 0) {
                if (sort_specs->SpecsCount > 0) {
                    const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
                    const int col = spec.ColumnIndex;
                    const bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

                    std::sort(libs.begin(), libs.end(), [col, ascending](const Lib& a, const Lib& b) {
                        const char* sa = (col == 0) ? a.name : (col == 1) ? a.description : a.license;
                        const char* sb = (col == 0) ? b.name : (col == 1) ? b.description : b.license;
                        int cmp = CaseInsensitiveCompare(sa, sb);
                        return ascending ? (cmp < 0) : (cmp > 0);
                    });
                } else {
                    // No sort (third click state) -> restore original order.
                    libs = libs_source;
                }
                sort_specs->SpecsDirty = false;
            }
        }

        for (const auto& lib : libs) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            ImGui::TextUnformatted(lib.name);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", lib.description);
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", lib.license);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
}
