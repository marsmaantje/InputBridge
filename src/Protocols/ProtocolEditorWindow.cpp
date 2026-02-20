#include "ProtocolEditorWindow.h"
#include "ProtocolRegistry.h"
#include "ProtocolDefinition.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <string>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static ProtocolField* FindField(ProtocolDefinition& def, const std::string& fieldId) {
    for (auto& f : def.fields)
        if (f.fieldId == fieldId) return &f;
    return nullptr;
}

static bool MatchesFilter(const FieldDescriptor& fd, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    // Case-insensitive substring match against label, category, or id
    auto contains = [](const std::string& s, const char* q) {
        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        std::string ql = q;
        std::transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
        return lower.find(ql) != std::string::npos;
    };
    return contains(fd.label, filter) || contains(fd.category, filter) || contains(fd.id, filter);
}

// ─── Main entry point ────────────────────────────────────────────────────────

void ProtocolEditorWindow::Draw(bool& open) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Protocol Editor", &open,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    // ── Toolbar ──────────────────────────────────────────────────────────────
    if (ImGui::Button("+ New Protocol")) {
        s_showNewModal = true;
        s_newName[0] = '\0';
        std::strcat(s_newName, "New Protocol");
        s_newTransport = 0;
        s_newDirection = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save All")) {
        ProtocolRegistry::GetInstance().SaveAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Fields")) {
        ProtocolRegistry::GetInstance().ReloadFieldCatalog();
    }
    ImGui::Separator();

    // ── Two-column layout: left=list, right=editor ────────────────────────
    float listWidth = 220.0f;
    ImGui::BeginChild("##ProtocolList", ImVec2(listWidth, 0), true);
    DrawProtocolList();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##ProtocolEditor", ImVec2(0, 0), true);
    DrawEditor();
    ImGui::EndChild();

    // ── Modals ───────────────────────────────────────────────────────────────
    DrawNewProtocolModal();

    ImGui::End();
}

// ─── Left panel: protocol list ───────────────────────────────────────────────

void ProtocolEditorWindow::DrawProtocolList() {
    auto& reg  = ProtocolRegistry::GetInstance();
    auto& defs = reg.GetDefinitions();

    ImGui::Text("Protocols (%d)", (int)defs.size());
    ImGui::Separator();

    for (int i = 0; i < (int)defs.size(); ++i) {
        const auto& def = defs[i];

        // Transport icon
        const char* icon = (def.transport == ProtocolTransport::OSC) ? "[OSC] " : "[WS]  ";
        // Direction indicator
        const char* dir  = (def.direction == ProtocolDirection::Output) ? "↑" : "↓";

        std::string label = std::string(icon) + dir + " " + def.name + "##" + def.id;

        bool selected = (s_selectedIndex == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
            s_selectedIndex = i;
        }
        if (selected) ImGui::PopStyleColor();

        // Delete button on right-click context menu
        if (ImGui::BeginPopupContextItem(def.id.c_str())) {
            if (ImGui::MenuItem("Delete")) {
                reg.DeleteDefinition(def.id);
                if (s_selectedIndex >= (int)reg.GetDefinitions().size())
                    s_selectedIndex = (int)reg.GetDefinitions().size() - 1;
                ImGui::EndPopup();
                break; // list invalidated
            }
            ImGui::EndPopup();
        }
    }
}

// ─── Right panel: editor ─────────────────────────────────────────────────────

void ProtocolEditorWindow::DrawEditor() {
    auto& reg  = ProtocolRegistry::GetInstance();
    auto& defs = reg.GetDefinitions();

    if (s_selectedIndex < 0 || s_selectedIndex >= (int)defs.size()) {
        ImGui::TextDisabled("Select or create a protocol on the left.");
        return;
    }

    ProtocolDefinition& def = defs[s_selectedIndex];

    // ── Header ───────────────────────────────────────────────────────────────
    ImGui::PushItemWidth(300.0f);
    char nameBuf[256];
    std::strncpy(nameBuf, def.name.c_str(), sizeof(nameBuf));
    nameBuf[sizeof(nameBuf)-1] = '\0';
    if (ImGui::InputText("Name##pname", nameBuf, sizeof(nameBuf))) {
        def.name = nameBuf;
        s_pendingSave = true;
    }
    ImGui::PopItemWidth();

    // Transport & direction (read-only after creation to keep file paths stable)
    ImGui::SameLine();
    ImGui::BeginDisabled(true);
    ImGui::Text("  Transport: %s", ProtocolRegistry::TransportLabel(def.transport));
    ImGui::SameLine();
    ImGui::Text("  Direction: %s", ProtocolRegistry::DirectionLabel(def.direction));
    ImGui::EndDisabled();

    ImGui::Separator();

    // ── Connection settings ───────────────────────────────────────────────────
    if (def.transport == ProtocolTransport::OSC) {
        ImGui::Text("OSC Settings");
        char hostBuf[128];
        std::strncpy(hostBuf, def.oscHost.c_str(), sizeof(hostBuf));
        hostBuf[sizeof(hostBuf)-1] = '\0';
        ImGui::PushItemWidth(140.0f);
        if (ImGui::InputText("Host##oschost", hostBuf, sizeof(hostBuf))) {
            def.oscHost = hostBuf;
            s_pendingSave = true;
        }
        ImGui::SameLine();
        if (def.direction == ProtocolDirection::Output || def.direction == ProtocolDirection::Input) {
            ImGui::PushItemWidth(100.0f);
            if (ImGui::InputInt("Send Port##oscsend", &def.oscSendPort)) s_pendingSave = true;
            ImGui::SameLine();
            ImGui::PushItemWidth(100.0f);
            if (ImGui::InputInt("Recv Port##oscrecv", &def.oscRecvPort)) s_pendingSave = true;
        }
        ImGui::PopItemWidth();
    } else {
        ImGui::Text("WebSocket Settings");
        ImGui::PushItemWidth(120.0f);
        if (ImGui::InputInt("Port##wsport", &def.wssPort)) s_pendingSave = true;
        ImGui::PopItemWidth();
    }

    ImGui::Separator();

    // ── Save / discard bar ────────────────────────────────────────────────────
    if (s_pendingSave) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button("Save Changes")) {
            reg.SaveDefinition(def);
            s_pendingSave = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            // Reload from disk
            reg.LoadAll();
            s_pendingSave = false;
        }
        ImGui::SameLine();
    }

    // Delete button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("Delete Protocol")) {
        ImGui::OpenPopup("ConfirmDelete");
    }
    ImGui::PopStyleColor();

    if (ImGui::BeginPopupModal("ConfirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"?", def.name.c_str());
        ImGui::Text("This will remove the file from disk.");
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            std::string id = def.id;
            reg.DeleteDefinition(id);
            s_selectedIndex = std::max(0, s_selectedIndex - 1);
            s_pendingSave = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // ── Field picker ─────────────────────────────────────────────────────────
    ImGui::Text("Data Fields");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##fieldfilter", "Filter fields...", s_fieldFilter, sizeof(s_fieldFilter));

    ImGui::BeginChild("##FieldArea", ImVec2(0, 0), false);

    if (def.direction == ProtocolDirection::Output)
        DrawOutputFieldPicker();
    else
        DrawInputFieldPicker();

    ImGui::EndChild();
}

// ─── Field pickers ────────────────────────────────────────────────────────────

static void DrawFieldTable(ProtocolDefinition& def,
                           const std::vector<FieldDescriptor>& catalog,
                           bool isOsc,
                           const char* filter,
                           bool& pendingSave) {
    auto& reg = ProtocolRegistry::GetInstance();

    // Group by category
    std::string currentCat;
    for (const auto& fd : catalog) {
        if (!MatchesFilter(fd, filter)) continue;

        if (fd.category != currentCat) {
            currentCat = fd.category;
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", currentCat.c_str());
            ImGui::Separator();
        }

        ProtocolField* existing = FindField(def, fd.id);
        bool enabled = existing && existing->enabled;

        // ── Enabled checkbox ─────────────────────────────────────────────
        ImGui::PushID(fd.id.c_str());
        if (ImGui::Checkbox("##en", &enabled)) {
            if (!existing) {
                // Add new entry
                ProtocolField pf;
                pf.fieldId = fd.id;
                pf.oscPath = fd.defaultOscPath;
                pf.wsKey   = fd.defaultWsKey;
                pf.enabled = enabled;
                def.fields.push_back(pf);
                existing = &def.fields.back();
            } else {
                existing->enabled = enabled;
            }
            pendingSave = true;
        }
        ImGui::SameLine();

        // ── Label ────────────────────────────────────────────────────────
        if (!enabled) ImGui::BeginDisabled();
        ImGui::Text("%s", fd.label.c_str());
        if (!enabled) ImGui::EndDisabled();

        // ── Address/key override (only when enabled) ─────────────────────
        if (enabled && existing) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            char buf[256];
            if (isOsc) {
                std::strncpy(buf, existing->oscPath.empty() ? fd.defaultOscPath.c_str() : existing->oscPath.c_str(), sizeof(buf));
                buf[sizeof(buf)-1] = '\0';
                std::string lbl = "##osc_" + fd.id;
                if (ImGui::InputText(lbl.c_str(), buf, sizeof(buf))) {
                    existing->oscPath = buf;
                    pendingSave = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("OSC address. Default: %s", fd.defaultOscPath.c_str());
            } else {
                std::strncpy(buf, existing->wsKey.empty() ? fd.defaultWsKey.c_str() : existing->wsKey.c_str(), sizeof(buf));
                buf[sizeof(buf)-1] = '\0';
                std::string lbl = "##ws_" + fd.id;
                if (ImGui::InputText(lbl.c_str(), buf, sizeof(buf))) {
                    existing->wsKey = buf;
                    pendingSave = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("WebSocket JSON key. Default: %s", fd.defaultWsKey.c_str());
            }
            // Reset-to-default button
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##r")) {
                existing->oscPath = fd.defaultOscPath;
                existing->wsKey   = fd.defaultWsKey;
                pendingSave = true;
            }
        }

        ImGui::PopID();
    }
}

void ProtocolEditorWindow::DrawOutputFieldPicker() {
    auto& reg  = ProtocolRegistry::GetInstance();
    auto& defs = reg.GetDefinitions();
    if (s_selectedIndex < 0 || s_selectedIndex >= (int)defs.size()) return;

    ProtocolDefinition& def = defs[s_selectedIndex];
    bool isOsc = (def.transport == ProtocolTransport::OSC);

    ImGui::TextDisabled("These fields are sent FROM the server TO the client (game/sim input data).");
    ImGui::Spacing();

    DrawFieldTable(def, reg.GetOutputFields(), isOsc, s_fieldFilter, s_pendingSave);
}

void ProtocolEditorWindow::DrawInputFieldPicker() {
    auto& reg  = ProtocolRegistry::GetInstance();
    auto& defs = reg.GetDefinitions();
    if (s_selectedIndex < 0 || s_selectedIndex >= (int)defs.size()) return;

    ProtocolDefinition& def = defs[s_selectedIndex];
    bool isOsc = (def.transport == ProtocolTransport::OSC);

    ImGui::TextDisabled("These fields are received BY the server FROM the client (haptic / rumble commands).");
    ImGui::Spacing();

    DrawFieldTable(def, reg.GetInputFields(), isOsc, s_fieldFilter, s_pendingSave);
}

// ─── New protocol modal ───────────────────────────────────────────────────────

void ProtocolEditorWindow::DrawNewProtocolModal() {
    if (s_showNewModal) {
        ImGui::OpenPopup("New Protocol##modal");
        s_showNewModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("New Protocol##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Create a new protocol definition");
        ImGui::Separator();

        ImGui::InputText("Name",      s_newName,    sizeof(s_newName));

        const char* transports[] = { "OSC", "WebSocket" };
        ImGui::Combo("Transport",    &s_newTransport, transports, 2);

        const char* directions[] = { "Output (server → client)", "Input (client → server)" };
        ImGui::Combo("Direction",    &s_newDirection, directions, 2);

        ImGui::Separator();

        bool nameOk = (s_newName[0] != '\0');
        if (!nameOk) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            auto transport = (s_newTransport == 1) ? ProtocolTransport::WebSocket : ProtocolTransport::OSC;
            auto direction = (s_newDirection == 1) ? ProtocolDirection::Input : ProtocolDirection::Output;

            std::string id = ProtocolRegistry::GetInstance()
                                 .CreateDefinition(s_newName, transport, direction);

            // Select the new definition
            auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
            for (int i = 0; i < (int)defs.size(); ++i) {
                if (defs[i].id == id) { s_selectedIndex = i; break; }
            }

            ImGui::CloseCurrentPopup();
        }
        if (!nameOk) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
