#include "NetworkStatusWindow.h"
#include "OSCServer.h"
#include "WebSocketServer.h"
#include "imgui.h"
#include "Protocols/ProtocolEditorWindow.h"
#include "Protocols/ProtocolRegistry.h"

void NetworkStatusWindow::Draw(int& update_rate, bool& dynamic_rate, float messages_per_second) {
    if (ImGui::Begin("Network Server")) {
        ImGui::Text("Update Rate");
        ImGui::SameLine();
        ImGui::Checkbox("Dynamic Rate", &dynamic_rate);
        if (dynamic_rate) {
            ImGui::SameLine();
            ImGui::Text("Messages/sec: %.1f", messages_per_second);
        } else {
            ImGui::SameLine();
            const char* label = "Target Rate (Hz)";
            float width = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemInnerSpacing.x;
            if (width < 10.0f) width = 10.0f;
            ImGui::SetNextItemWidth(width);
            ImGui::SliderInt(label, &update_rate, 1, 200);
        }
        ImGui::Separator();
        if (ImGui::BeginTabBar("NetworkTabs")) {
            if (ImGui::BeginTabItem("OSC Server")) {
                OSCServer::GetInstance().DrawContent();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("WebSocket Server")) {
                WebSocketServer::GetInstance().DrawContent();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Protocols")) {
                // Inline protocol editor (no separate window open/close flag needed
                // since it lives inside this tab)
                static bool editorOpen = true;
                // Draw without the outer Begin/End window wrapper by calling
                // the child-content helpers directly – we re-use the window
                // space already provided by the tab.
                DrawProtocolEditorInline();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// Inline variant: draws the protocol editor content directly inside the
// current ImGui window (no separate window, no open/close flag).
void NetworkStatusWindow::DrawProtocolEditorInline() {
    auto& reg  = ProtocolRegistry::GetInstance();
    auto& defs = reg.GetDefinitions();

    // ── Toolbar ───────────────────────────────────────────────────────────
    if (ImGui::Button("Open Full Editor")) {
        s_showFullEditor = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d protocol(s) defined)", (int)defs.size());
    ImGui::Spacing();

    // Quick summary table
    if (defs.empty()) {
        ImGui::TextDisabled("No protocols defined yet. Open the full editor to create one.");
    } else {
        if (ImGui::BeginTable("##ProtoSummary", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Transport");
            ImGui::TableSetupColumn("Direction");
            ImGui::TableSetupColumn("Fields");
            ImGui::TableHeadersRow();

            for (const auto& d : defs) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(d.name.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(ProtocolRegistry::TransportLabel(d.transport));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(ProtocolRegistry::DirectionLabel(d.direction));
                int enabled = 0;
                for (const auto& f : d.fields) if (f.enabled) ++enabled;
                ImGui::TableNextColumn(); ImGui::Text("%d / %d enabled", enabled, (int)d.fields.size());
            }
            ImGui::EndTable();
        }
    }

    // Full-screen editor (separate window)
    if (s_showFullEditor) {
        ProtocolEditorWindow::Draw(s_showFullEditor);
    }
}
