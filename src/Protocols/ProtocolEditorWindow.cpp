#include "ProtocolEditorWindow.h"
#include "ProtocolRegistry.h"
#include "ProtocolDefinition.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cctype>
#include <cstdio>
namespace fs = std::filesystem;
using json = nlohmann::json;

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

static const char* s_settingsFile = "protocol_editor_settings.json";

static void LoadSettings(std::string& importDir, std::string& exportDir) {
    if (!fs::exists(s_settingsFile)) return;
    try {
        std::ifstream i(s_settingsFile);
        json j;
        i >> j;
        if (j.contains("importDir")) importDir = j["importDir"];
        if (j.contains("exportDir")) exportDir = j["exportDir"];
    } catch (...) {}
}

static void SaveSettings(const std::string& importDir, const std::string& exportDir) {
    try {
        json j;
        j["importDir"] = importDir;
        j["exportDir"] = exportDir;
        std::ofstream o(s_settingsFile);
        o << j.dump(4);
    } catch (...) {}
}

static bool DrawFileBrowser(std::string& currentDir, char* pathBuf, size_t pathBufSize) {
    bool changed = false;
    if (ImGui::Button("Up")) {
        std::error_code ec;
        fs::path p = fs::absolute(fs::path(currentDir), ec);
        if (!ec && p.has_parent_path()) { currentDir = p.parent_path().string(); changed = true; }
    }
    ImGui::SameLine();
    ImGui::Text("Dir: %s", currentDir.c_str());

    ImGui::BeginChild("##filebrowser", ImVec2(0, -40), true);
    try {
        fs::path path(currentDir);
        if (fs::exists(path) && fs::is_directory(path)) {
            std::vector<fs::directory_entry> dirs, files;
            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.is_directory()) dirs.push_back(entry);
                else if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry);
            }
            auto sort_entries = [](const fs::directory_entry& a, const fs::directory_entry& b) {
                return a.path().filename() < b.path().filename();
            };
            std::sort(dirs.begin(), dirs.end(), sort_entries);
            std::sort(files.begin(), files.end(), sort_entries);

            for (const auto& entry : dirs) {
                if (ImGui::Selectable(("[Dir] " + entry.path().filename().string()).c_str())) {
                    currentDir = entry.path().string();
                    changed = true;
                }
            }
            for (const auto& entry : files) {
                if (ImGui::Selectable(entry.path().filename().string().c_str())) {
                    std::string absPath = fs::absolute(entry.path()).string();
                    std::strncpy(pathBuf, absPath.c_str(), pathBufSize);
                    pathBuf[pathBufSize-1] = '\0';
                }
            }
        }
    } catch (...) { ImGui::TextColored(ImVec4(1,0,0,1), "Error reading directory"); }
    ImGui::EndChild();
    return changed;
}

// ─── Main entry point ────────────────────────────────────────────────────────

void ProtocolEditorWindow::Draw(bool& open) {
    static bool s_settingsLoaded = false;
    if (!s_settingsLoaded) {
        LoadSettings(s_importCurrentDir, s_exportCurrentDir);
        s_settingsLoaded = true;
    }

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
    DrawDuplicateProtocolModal();
    DrawCreateFieldModal();
    DrawSavePresetModal();
    DrawExportProtocolModal();
    DrawImportProtocolModal();
    DrawRenameCategoryModal();

    ImGui::End();
}

// ─── Left panel: protocol list ───────────────────────────────────────────────

void ProtocolEditorWindow::DrawProtocolList() {
    auto& reg  = ProtocolRegistry::GetInstance();
    auto& defs = reg.GetDefinitions();

    ImGui::Text("Protocols (%d)", (int)defs.size());
    ImGui::Separator();

    if (ImGui::Button("Import...")) {
        s_showImportModal = true;
        s_importPath[0] = '\0';
    }

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
            if (ImGui::MenuItem("Duplicate")) {
                s_showDupModal = true;
                s_dupSourceId = def.id;
                std::string copyName = def.name + " (Copy)";
                std::strncpy(s_dupName, copyName.c_str(), sizeof(s_dupName));
                s_dupName[sizeof(s_dupName)-1] = '\0';
                s_dupTransport = (def.transport == ProtocolTransport::OSC) ? 0 : 1;
            }
            if (ImGui::MenuItem("Export...")) {
                s_showExportModal = true;
                s_exportId = def.id;
                std::string filename = def.name + ".json";
                std::strncpy(s_exportPath, filename.c_str(), sizeof(s_exportPath));
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
    ImGui::SameLine();
    if (ImGui::Button("Save as Preset")) {
        s_showSavePresetModal = true;
        std::strncpy(s_presetName, "New Preset", sizeof(s_presetName));
    }

    ImGui::BeginChild("##FieldArea", ImVec2(0, 0), false);

    if (def.direction == ProtocolDirection::Output)
        DrawOutputFieldPicker();
    else
        DrawInputFieldPicker();

    ImGui::EndChild();
}

// ─── Field pickers ────────────────────────────────────────────────────────────

void ProtocolEditorWindow::DrawFieldTable(ProtocolDefinition& def,
                           const std::vector<FieldDescriptor>& catalog,
                           bool isOsc,
                           const char* filter,
                           bool& pendingSave) {
    auto& reg = ProtocolRegistry::GetInstance();

    if (ImGui::Button("+ Create Field")) {
        s_showCreateFieldModal = true;
        s_cfId[0] = '\0';
        s_cfLabel[0] = '\0';
        std::strcpy(s_cfCategory, "Custom");
        s_cfType = 0;
        std::strcpy(s_cfOsc, "/custom/");
        std::strcpy(s_cfWs, "custom_");
    }
    ImGui::Separator();

    // Group by category
    std::string currentCat;
    bool isCatOpen = false;
    for (const auto& fd : catalog) {
        if (!MatchesFilter(fd, filter)) continue;

        if (fd.category != currentCat) {
            if (isCatOpen) {
                ImGui::TreePop();
            }
            currentCat = fd.category;
            isCatOpen = false;
            ImGui::Separator();
            if (ImGui::TreeNode(currentCat.c_str())) {
                isCatOpen = true;
                ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", currentCat.c_str());
                ImGui::SameLine();
                ImGui::PushID(currentCat.c_str());
                if (ImGui::SmallButton("Rename")) {
                    s_showRenameCatModal = true;
                    std::strncpy(s_renCatOldName, currentCat.c_str(), sizeof(s_renCatOldName));
                    s_renCatOldName[sizeof(s_renCatOldName)-1] = '\0';
                    std::strncpy(s_renCatNewName, currentCat.c_str(), sizeof(s_renCatNewName));
                    s_renCatNewName[sizeof(s_renCatNewName)-1] = '\0';
                }
                ImGui::PopID();
                ImGui::Separator();
            }
        }

        if (!isCatOpen) continue;

        ImGui::PushID(fd.id.c_str());
        ProtocolField* existing = FindField(def, fd.id);
        bool enabled = existing && existing->enabled;

        // ── Enabled checkbox ─────────────────────────────────────────────
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

            // Duplicate/Delete for custom fields
            if (!fd.isBuiltIn) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Dup##d")) {
                    s_showCreateFieldModal = true;
                    std::string newId = fd.id + "_copy";
                    std::strncpy(s_cfId, newId.c_str(), sizeof(s_cfId));
                    std::strncpy(s_cfLabel, fd.label.c_str(), sizeof(s_cfLabel));
                    std::strncpy(s_cfCategory, fd.category.c_str(), sizeof(s_cfCategory));
                    s_cfType = (fd.type == FieldType::DigitalButton) ? 1 : 0;
                    std::strncpy(s_cfOsc, fd.defaultOscPath.c_str(), sizeof(s_cfOsc));
                    std::strncpy(s_cfWs, fd.defaultWsKey.c_str(), sizeof(s_cfWs));
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Del##d")) {
                    reg.DeleteOutputField(fd.id);
                    // If this field was enabled in current def, it will be ignored on load
                    // but we should probably mark pending save to clean up the definition
                    pendingSave = true;
                }
            }
        }

        ImGui::PopID();
    }
    if (isCatOpen) {
        ImGui::TreePop();
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

        // Presets
        auto& presets = ProtocolRegistry::GetInstance().GetPresets();
        if (!presets.empty()) {
            if (ImGui::BeginCombo("Preset", (s_newPresetIdx == 0) ? "None" : presets[s_newPresetIdx-1].name.c_str())) {
                if (ImGui::Selectable("None", s_newPresetIdx == 0)) s_newPresetIdx = 0;
                for (int i = 0; i < (int)presets.size(); ++i) {
                    if (ImGui::Selectable(presets[i].name.c_str(), s_newPresetIdx == i + 1)) s_newPresetIdx = i + 1;
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();

        bool nameOk = (s_newName[0] != '\0');
        if (!nameOk) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            auto transport = (s_newTransport == 1) ? ProtocolTransport::WebSocket : ProtocolTransport::OSC;
            auto direction = (s_newDirection == 1) ? ProtocolDirection::Input : ProtocolDirection::Output;

            std::string id = ProtocolRegistry::GetInstance()
                                 .CreateDefinition(s_newName, transport, direction);

            // Apply preset if selected
            if (s_newPresetIdx > 0 && s_newPresetIdx <= (int)presets.size()) {
                auto* def = ProtocolRegistry::GetInstance().FindById(id);
                if (def) {
                    const auto& p = presets[s_newPresetIdx - 1];
                    for (const auto& fid : p.fieldIds) {
                        ProtocolField pf;
                        pf.fieldId = fid;
                        pf.enabled = true;
                        def->fields.push_back(pf);
                    }
                    ProtocolRegistry::GetInstance().SaveDefinition(*def);
                }
            }

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

void ProtocolEditorWindow::DrawDuplicateProtocolModal() {
    if (s_showDupModal) {
        ImGui::OpenPopup("Duplicate Protocol##modal");
        s_showDupModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Duplicate Protocol##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", s_dupName, sizeof(s_dupName));
        const char* transports[] = { "OSC", "WebSocket" };
        ImGui::Combo("Transport", &s_dupTransport, transports, 2);

        ImGui::Separator();
        if (ImGui::Button("Duplicate", ImVec2(120, 0))) {
            auto transport = (s_dupTransport == 1) ? ProtocolTransport::WebSocket : ProtocolTransport::OSC;
            std::string id = ProtocolRegistry::GetInstance().DuplicateDefinition(s_dupSourceId, s_dupName, transport);
            
            // Select new
            auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
            for (int i = 0; i < (int)defs.size(); ++i) {
                if (defs[i].id == id) { s_selectedIndex = i; break; }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawRenameCategoryModal() {
    if (s_showRenameCatModal) {
        ImGui::OpenPopup("Rename Category##modal");
        s_showRenameCatModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Rename Category##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Rename category \"%s\" to:", s_renCatOldName);
        ImGui::InputText("New Name", s_renCatNewName, sizeof(s_renCatNewName));

        ImGui::Separator();
        if (ImGui::Button("Rename", ImVec2(120, 0))) {
            auto& reg = ProtocolRegistry::GetInstance();
            // const_cast is used here because we cannot modify the ProtocolRegistry header to add a non-const accessor
            auto& outFields = const_cast<std::vector<FieldDescriptor>&>(reg.GetOutputFields());
            for (auto& fd : outFields)
                if (fd.category == s_renCatOldName) fd.category = s_renCatNewName;
            
            auto& inFields = const_cast<std::vector<FieldDescriptor>&>(reg.GetInputFields());
            for (auto& fd : inFields)
                if (fd.category == s_renCatOldName) fd.category = s_renCatNewName;

            reg.SaveFieldCatalog();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawCreateFieldModal() {
    if (s_showCreateFieldModal) {
        ImGui::OpenPopup("Create/Edit Field##modal");
        s_showCreateFieldModal = false;
        s_cfIdManuallyModified = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Create/Edit Field##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        bool idChanged = false;
        if (ImGui::InputText("ID", s_cfId, sizeof(s_cfId))) {
            s_cfIdManuallyModified = true;
            idChanged = true;
        }
        if (ImGui::InputText("Label", s_cfLabel, sizeof(s_cfLabel))) {
            if (!s_cfIdManuallyModified) {
                std::string slug;
                for (char* p = s_cfLabel; *p; ++p) {
                    char c = *p;
                    if (std::isalnum((unsigned char)c)) slug += (char)std::tolower((unsigned char)c);
                    else if (c == ' ' || c == '-' || c == '_') {
                        if (!slug.empty() && slug.back() != '_') slug += '_';
                    }
                }
                if (slug.length() >= sizeof(s_cfId)) slug.resize(sizeof(s_cfId) - 1);
                std::strncpy(s_cfId, slug.c_str(), sizeof(s_cfId));
                idChanged = true;
            }
        }

        if (idChanged) {
            std::snprintf(s_cfOsc, sizeof(s_cfOsc), "/custom/%s", s_cfId);
            std::snprintf(s_cfWs, sizeof(s_cfWs), "custom_%s", s_cfId);
        }

        // Collect existing categories
        std::vector<std::string> categories;
        auto& reg = ProtocolRegistry::GetInstance();
        for (const auto& f : reg.GetOutputFields())
            if (std::find(categories.begin(), categories.end(), f.category) == categories.end()) categories.push_back(f.category);
        for (const auto& f : reg.GetInputFields())
            if (std::find(categories.begin(), categories.end(), f.category) == categories.end()) categories.push_back(f.category);
        std::sort(categories.begin(), categories.end());

        float buttonSize = ImGui::GetFrameHeight();
        float itemWidth = ImGui::CalcItemWidth();
        ImGui::SetNextItemWidth(itemWidth - buttonSize - ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::InputText("##Category", s_cfCategory, sizeof(s_cfCategory));
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(buttonSize);
        if (ImGui::BeginCombo("Category", nullptr, ImGuiComboFlags_NoPreview)) {
            for (const auto& cat : categories) {
                if (ImGui::Selectable(cat.c_str())) {
                    std::strncpy(s_cfCategory, cat.c_str(), sizeof(s_cfCategory));
                    s_cfCategory[sizeof(s_cfCategory)-1] = '\0';
                }
            }
            ImGui::EndCombo();
        }

        const char* types[] = { "Analog Axis", "Digital Button" };
        ImGui::Combo("Type", &s_cfType, types, 2);
        ImGui::InputText("Default OSC Path", s_cfOsc, sizeof(s_cfOsc));
        ImGui::InputText("Default WS Key", s_cfWs, sizeof(s_cfWs));

        ImGui::Separator();
        
        bool idExists = false;
        for (const auto& f : reg.GetOutputFields()) { if (f.id == s_cfId) { idExists = true; break; } }
        if (!idExists) {
            for (const auto& f : reg.GetInputFields()) { if (f.id == s_cfId) { idExists = true; break; } }
        }
        if (idExists) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "ID already exists!");

        bool ok = (s_cfId[0] != '\0') && !idExists;
        if (!ok) ImGui::BeginDisabled();
        
        if (ImGui::Button("Save Field", ImVec2(120, 0))) {
            FieldDescriptor fd;
            fd.id = s_cfId;
            fd.label = s_cfLabel;
            fd.category = s_cfCategory;
            fd.type = (s_cfType == 1) ? FieldType::DigitalButton : FieldType::AnalogAxis;
            fd.defaultOscPath = s_cfOsc;
            fd.defaultWsKey = s_cfWs;
            fd.isBuiltIn = false;
            
            ProtocolRegistry::GetInstance().AddOutputField(fd);
            ImGui::CloseCurrentPopup();
        }
        if (!ok) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawExportProtocolModal() {
    if (s_showExportModal) {
        ImGui::OpenPopup("Export Protocol##modal");
        s_showExportModal = false;
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Export Protocol##modal", &open)) {
        ImGui::Text("Export to JSON file");
        ImGui::InputText("File Path", s_exportPath, sizeof(s_exportPath));
        
        ImGui::Separator();
        if (DrawFileBrowser(s_exportCurrentDir, s_exportPath, sizeof(s_exportPath))) {
            SaveSettings(s_importCurrentDir, s_exportCurrentDir);
        }

        ImGui::Separator();
        if (ImGui::Button("Export", ImVec2(120, 0))) {
            ProtocolRegistry::GetInstance().ExportDefinition(s_exportId, s_exportPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawImportProtocolModal() {
    if (s_showImportModal) {
        ImGui::OpenPopup("Import Protocol##modal");
        s_showImportModal = false;
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Import Protocol##modal", &open)) {
        ImGui::Text("Import from JSON file");
        ImGui::InputText("File Path", s_importPath, sizeof(s_importPath));

        ImGui::Separator();
        if (DrawFileBrowser(s_importCurrentDir, s_importPath, sizeof(s_importPath))) {
            SaveSettings(s_importCurrentDir, s_exportCurrentDir);
        }
        
        ImGui::Separator();
        if (ImGui::Button("Import", ImVec2(120, 0))) {
            std::string id = ProtocolRegistry::GetInstance().ImportDefinition(s_importPath);
            auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
            for (int i = 0; i < (int)defs.size(); ++i) {
                if (defs[i].id == id) { s_selectedIndex = i; break; }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawSavePresetModal() {
    if (s_showSavePresetModal) {
        ImGui::OpenPopup("Save Preset##modal");
        s_showSavePresetModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Save Preset##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save current enabled fields as a preset");
        ImGui::InputText("Preset Name", s_presetName, sizeof(s_presetName));
        
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            auto& reg = ProtocolRegistry::GetInstance();
            auto& defs = reg.GetDefinitions();
            if (s_selectedIndex >= 0 && s_selectedIndex < (int)defs.size()) {
                const auto& def = defs[s_selectedIndex];
                std::vector<std::string> fields;
                for (const auto& f : def.fields) {
                    if (f.enabled) fields.push_back(f.fieldId);
                }
                reg.SavePreset(s_presetName, fields);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
