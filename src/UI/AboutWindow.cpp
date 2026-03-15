#include "AboutWindow.h"
#include "imgui.h"

void AboutWindow::DrawContent() {
    // ── Title ────────────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("InputBridge");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // ── Description ─────────────────────────────────────────────────────────
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
            "At some point the feature creep started taking over and it may has gone a bit out of control ever since."
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

    // ── The Science Team ────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("The Science Team");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    struct Member { const char* name; const char* role; };
    constexpr Member members[] = {
        { "Elektrospy",  "Developer" },
        { "marsmaantja", "Developer" },
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

    // ── Alpha Testers ────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("Alpha Testers");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    constexpr const char* testers[] = {
        "Beaned", "Dawky", "QueenHidi", "Snuhr", "Water"
    };
    ImGui::Indent(8.0f);
    for (const char* t : testers) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::Text("%s", t);
    }
    ImGui::Unindent(8.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Libraries ────────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
    ImGui::Text("Open-Source Libraries");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    struct Lib { const char* name; const char* description; const char* license; };
    constexpr Lib libs[] = {
        { "SDL3",              "Cross-platform hardware abstraction (input, audio, video)", "zlib License"   },
        { "Dear ImGui",        "Immediate-mode graphical user interface library",           "MIT License"    },
        { "liblo",             "Lightweight OSC (Open Sound Control) implementation",      "LGPL 2.1"       },
        { "nlohmann/json",     "Single-header JSON parser and serialiser for C++",         "MIT License"    },
        { "uWebSockets",       "High-performance WebSocket server library",                "Apache 2.0"     },
        { "uSockets",          "Low-level async networking (used by uWebSockets)",         "Apache 2.0"     },
        { "wheel-rpm-lib",     "Steering wheel RPM LED control via HID",                  "Internal"       },
        { "Google Fonts",      "Used for the Resonite and VRChat color themes",            "Apache 2.0 / OFL 1.1"   },
        { "Font Awesome 6",    "Icon font used throughout the user interface",             "SIL OFL 1.1"    },
    };

    if (ImGui::BeginTable("##libs", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Library",     ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 4.0f);
        ImGui::TableSetupColumn("License",     ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();

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