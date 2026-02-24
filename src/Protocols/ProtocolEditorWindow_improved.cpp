#include "ProtocolEditorWindow.h"
#include "ProtocolRegistry.h"
#include "ProtocolDefinition.h"
#include "../Utils/FileDialog.h"
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

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Find a field within a protocol definition by ID.
 * 
 * @param def Protocol definition to search
 * @param fieldId Field identifier to find
 * @return Pointer to field if found, nullptr otherwise
 */
static ProtocolField* FindField(ProtocolDefinition& def, const std::string& fieldId) {
    for (auto& field : def.fields) {
        if (field.fieldId == fieldId) {
            return &field;
        }
    }
    return nullptr;
}

/**
 * Check if a field descriptor matches the search filter.
 * Case-insensitive substring match against label, category, or id.
 * 
 * @param fd Field descriptor to check
 * @param filter Search string (null or empty matches all)
 * @return true if field matches filter
 */
static bool MatchesFilter(const FieldDescriptor& fd, const char* filter) {
    if (!filter || filter[0] == '\0') {
        return true;
    }
    
    // Lambda for case-insensitive contains check
    auto contains = [](const std::string& haystack, const char* needle) {
        std::string lowerHaystack = haystack;
        std::transform(lowerHaystack.begin(), lowerHaystack.end(), 
                      lowerHaystack.begin(), ::tolower);
        
        std::string lowerNeedle = needle;
        std::transform(lowerNeedle.begin(), lowerNeedle.end(), 
                      lowerNeedle.begin(), ::tolower);
        
        return lowerHaystack.find(lowerNeedle) != std::string::npos;
    };
    
    return contains(fd.label, filter) || 
           contains(fd.category, filter) || 
           contains(fd.id, filter);
}

// ═══════════════════════════════════════════════════════════════════════════
// Settings Persistence
// ═══════════════════════════════════════════════════════════════════════════

static const char* SETTINGS_FILE = "protocol_editor_settings.json";

void ProtocolEditorWindow::LoadSettings() {
    if (!fs::exists(SETTINGS_FILE)) {
        return;
    }
    
    try {
        std::ifstream inputFile(SETTINGS_FILE);
        json settingsJson;
        inputFile >> settingsJson;
        
        if (settingsJson.contains("importDir")) {
            s_importCurrentDir = settingsJson["importDir"];
        }
        if (settingsJson.contains("exportDir")) {
            s_exportCurrentDir = settingsJson["exportDir"];
        }
    } catch (const std::exception& e) {
        // Silently ignore settings load errors
        fprintf(stderr, "Failed to load protocol editor settings: %s\n", e.what());
    }
}

void ProtocolEditorWindow::SaveSettings() {
    try {
        json settingsJson;
        settingsJson["importDir"] = s_importCurrentDir;
        settingsJson["exportDir"] = s_exportCurrentDir;
        
        std::ofstream outputFile(SETTINGS_FILE);
        outputFile << settingsJson.dump(4);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to save protocol editor settings: %s\n", e.what());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// File Browser (ImGui Fallback)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Draw an ImGui-based file browser as fallback when native dialogs unavailable.
 * 
 * @param currentDir Current directory being browsed
 * @param pathBuf Buffer to store selected file path
 * @param pathBufSize Size of path buffer
 * @return true if directory changed
 */
bool ProtocolEditorWindow::DrawFileBrowser(std::string& currentDir, 
                                            char* pathBuf, 
                                            size_t pathBufSize) {
    bool directoryChanged = false;
    
    // Up button to navigate to parent directory
    if (ImGui::Button("Up")) {
        std::error_code ec;
        fs::path currentPath = fs::absolute(fs::path(currentDir), ec);
        if (!ec && currentPath.has_parent_path()) {
            currentDir = currentPath.parent_path().string();
            directoryChanged = true;
        }
    }
    
    ImGui::SameLine();
    ImGui::Text("Dir: %s", currentDir.c_str());

    // File/directory listing
    ImGui::BeginChild("##filebrowser", ImVec2(0, -40), true);
    
    try {
        fs::path path(currentDir);
        if (!fs::exists(path) || !fs::is_directory(path)) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Invalid directory");
            ImGui::EndChild();
            return directoryChanged;
        }
        
        // Collect and sort entries
        std::vector<fs::directory_entry> directories;
        std::vector<fs::directory_entry> files;
        
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                directories.push_back(entry);
            } else if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry);
            }
        }
        
        // Sort entries alphabetically
        auto sortEntries = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        };
        std::sort(directories.begin(), directories.end(), sortEntries);
        std::sort(files.begin(), files.end(), sortEntries);

        // Display directories
        for (const auto& entry : directories) {
            if (ImGui::Selectable(("[Dir] " + entry.path().filename().string()).c_str())) {
                currentDir = entry.path().string();
                directoryChanged = true;
            }
        }
        
        // Display files
        for (const auto& entry : files) {
            if (ImGui::Selectable(entry.path().filename().string().c_str())) {
                std::string absolutePath = fs::absolute(entry.path()).string();
                std::strncpy(pathBuf, absolutePath.c_str(), pathBufSize);
                pathBuf[pathBufSize - 1] = '\0';
            }
        }
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error reading directory: %s", e.what());
    }
    
    ImGui::EndChild();
    return directoryChanged;
}

/**
 * Try to use native file dialog if available.
 * 
 * @param isSave true for save dialog, false for open dialog
 * @param path Input/output path
 * @return true if native dialog was shown and user selected a file
 */
bool ProtocolEditorWindow::TryNativeFileDialog(bool isSave, std::string& path) {
    if (!FileDialog::IsNativeDialogAvailable()) {
        return false;
    }
    
    std::vector<std::pair<std::string, std::string>> filters = {
        {"JSON Files", "*.json"},
        {"All Files", "*.*"}
    };
    
    FileDialog::Type dialogType = isSave ? FileDialog::Type::Save : FileDialog::Type::Open;
    std::string title = isSave ? "Export Protocol" : "Import Protocol";
    
    return FileDialog::Show(dialogType, title, path, filters, path);
}

// ═══════════════════════════════════════════════════════════════════════════
// Main Entry Point
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::Draw(bool& open) {
    if (!s_settingsLoaded) {
        LoadSettings();
        s_settingsLoaded = true;
    }

    if (!open) {
        return;
    }

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
        std::strncpy(s_newName, "New Protocol", sizeof(s_newName));
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
    ImGui::SameLine();
    
    if (ImGui::Button("Import...")) {
        ShowImportDialog();
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Export...")) {
        ShowExportDialog();
    }
    
    ImGui::Separator();

    // ── Two-column layout: left=list, right=editor ────────────────────────
    const float listWidth = 220.0f;
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
    DrawDeleteCategoryModal();

    ImGui::End();
}

void ProtocolEditorWindow::ShowImportDialog() {
    s_showImportModal = true;
    s_importPath[0] = '\0';
}

void ProtocolEditorWindow::ShowExportDialog() {
    auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
    if (s_selectedIndex >= 0 && s_selectedIndex < (int)defs.size()) {
        s_showExportModal = true;
        s_exportId = defs[s_selectedIndex].id;
        s_exportPath[0] = '\0';
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Left Panel: Protocol List
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawProtocolList() {
    auto& registry = ProtocolRegistry::GetInstance();
    auto& definitions = registry.GetDefinitions();

    ImGui::Text("Protocols (%d)", (int)definitions.size());
    ImGui::Separator();

    if (ImGui::Button("Import...")) {
        ShowImportDialog();
    }

    for (int i = 0; i < (int)definitions.size(); ++i) {
        const auto& definition = definitions[i];

        // Transport icon
        const char* icon = (definition.transport == ProtocolTransport::OSC) ? "[OSC] " : "[WS]  ";
        
        // Direction indicator
        const char* direction = (definition.direction == ProtocolDirection::Output) ? "↑" : "↓";

        std::string label = std::string(icon) + direction + " " + definition.name + "##" + definition.id;

        bool isSelected = (s_selectedIndex == i);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        }
        
        if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
            s_selectedIndex = i;
        }
        
        if (isSelected) {
            ImGui::PopStyleColor();
        }

        // Context menu for protocol actions
        if (ImGui::BeginPopupContextItem(definition.id.c_str())) {
            if (ImGui::MenuItem("Delete")) {
                registry.DeleteDefinition(definition.id);
                if (s_selectedIndex >= (int)registry.GetDefinitions().size()) {
                    s_selectedIndex = (int)registry.GetDefinitions().size() - 1;
                }
                ImGui::EndPopup();
                break; // List invalidated
            }
            
            if (ImGui::MenuItem("Duplicate")) {
                s_showDupModal = true;
                s_dupSourceId = definition.id;
                std::strncpy(s_dupName, (definition.name + " Copy").c_str(), sizeof(s_dupName));
                s_dupTransport = (definition.transport == ProtocolTransport::OSC) ? 0 : 1;
            }
            
            if (ImGui::MenuItem("Export")) {
                s_showExportModal = true;
                s_exportId = definition.id;
                s_exportPath[0] = '\0';
            }
            
            ImGui::EndPopup();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Right Panel: Protocol Editor
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawEditor() {
    auto& registry = ProtocolRegistry::GetInstance();
    auto& definitions = registry.GetDefinitions();

    if (s_selectedIndex < 0 || s_selectedIndex >= (int)definitions.size()) {
        ImGui::TextWrapped("Select a protocol from the list or create a new one.");
        return;
    }

    auto& definition = definitions[s_selectedIndex];
    bool needsSave = false;

    // ── Protocol name ────────────────────────────────────────────────────────
    ImGui::SeparatorText("Protocol Settings");
    
    char nameBuffer[128];
    std::strncpy(nameBuffer, definition.name.c_str(), sizeof(nameBuffer));
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        definition.name = nameBuffer;
        needsSave = true;
    }

    ImGui::Text("Transport: %s", 
                definition.transport == ProtocolTransport::OSC ? "OSC" : "WebSocket");
    ImGui::Text("Direction: %s", 
                definition.direction == ProtocolDirection::Output ? "Output" : "Input");

    // ── Transport-specific settings ──────────────────────────────────────────
    if (definition.transport == ProtocolTransport::OSC) {
        char hostBuffer[128];
        std::strncpy(hostBuffer, definition.oscHost.c_str(), sizeof(hostBuffer));
        if (ImGui::InputText("Host", hostBuffer, sizeof(hostBuffer))) {
            definition.oscHost = hostBuffer;
            needsSave = true;
        }

        if (definition.direction == ProtocolDirection::Output) {
            if (ImGui::InputInt("Send Port", &definition.oscSendPort)) {
                needsSave = true;
            }
        } else {
            if (ImGui::InputInt("Receive Port", &definition.oscRecvPort)) {
                needsSave = true;
            }
        }
    } else { // WebSocket
        if (ImGui::InputInt("Port", &definition.wssPort)) {
            needsSave = true;
        }
    }

    ImGui::Separator();

    // ── Field selection ──────────────────────────────────────────────────────
    if (definition.direction == ProtocolDirection::Output) {
        DrawOutputFieldPicker();
    } else {
        DrawInputFieldPicker();
    }

    // Save if changes were made
    if (needsSave || s_pendingSave) {
        registry.SaveDefinition(definition);
        s_pendingSave = false;
    }
}

void ProtocolEditorWindow::DrawOutputFieldPicker() {
    auto& registry = ProtocolRegistry::GetInstance();
    auto& definitions = registry.GetDefinitions();
    auto& definition = definitions[s_selectedIndex];
    
    const auto& catalog = registry.GetOutputFields();
    bool isOsc = (definition.transport == ProtocolTransport::OSC);

    ImGui::SeparatorText("Output Fields");
    
    // Field management buttons
    if (ImGui::Button("+ Create Field")) {
        s_showCreateFieldModal = true;
        s_cfId[0] = '\0';
        s_cfLabel[0] = '\0';
        std::strncpy(s_cfCategory, "Custom", sizeof(s_cfCategory));
        s_cfType = 0;
        std::strncpy(s_cfOsc, "/custom/", sizeof(s_cfOsc));
        std::strncpy(s_cfWs, "custom_", sizeof(s_cfWs));
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Save as Preset")) {
        s_showSavePresetModal = true;
        std::strncpy(s_presetName, "New Preset", sizeof(s_presetName));
    }
    
    // Category management
    ImGui::SameLine();
    if (ImGui::Button("Rename Category")) {
        s_showRenameCatModal = true;
        s_renCatOldName[0] = '\0';
        s_renCatNewName[0] = '\0';
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Delete Category")) {
        s_showDeleteCatModal = true;
        s_delCatName[0] = '\0';
    }

    // Search filter
    ImGui::InputText("Filter", s_fieldFilter, sizeof(s_fieldFilter));

    ImGui::Separator();
    DrawFieldTable(definition, catalog, isOsc, s_fieldFilter, s_pendingSave);
}

void ProtocolEditorWindow::DrawInputFieldPicker() {
    auto& registry = ProtocolRegistry::GetInstance();
    auto& definitions = registry.GetDefinitions();
    auto& definition = definitions[s_selectedIndex];
    
    const auto& catalog = registry.GetInputFields();
    bool isOsc = (definition.transport == ProtocolTransport::OSC);

    ImGui::SeparatorText("Input Fields");
    
    // Category management for input fields
    if (ImGui::Button("Rename Category")) {
        s_showRenameCatModal = true;
        s_renCatOldName[0] = '\0';
        s_renCatNewName[0] = '\0';
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Delete Category")) {
        s_showDeleteCatModal = true;
        s_delCatName[0] = '\0';
    }

    // Search filter
    ImGui::InputText("Filter", s_fieldFilter, sizeof(s_fieldFilter));

    ImGui::Separator();
    DrawFieldTable(definition, catalog, isOsc, s_fieldFilter, s_pendingSave);
}

// ═══════════════════════════════════════════════════════════════════════════
// Field Table
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawFieldTable(ProtocolDefinition& def,
                                           const std::vector<FieldDescriptor>& catalog,
                                           bool isOsc,
                                           const char* filter,
                                           bool& pendingSave) {
    // Group fields by category
    std::vector<std::string> categories;
    for (const auto& fd : catalog) {
        if (std::find(categories.begin(), categories.end(), fd.category) == categories.end()) {
            categories.push_back(fd.category);
        }
    }
    std::sort(categories.begin(), categories.end());

    // Draw categorized fields
    for (const auto& category : categories) {
        // Count fields in this category that match filter
        int matchCount = 0;
        for (const auto& fd : catalog) {
            if (fd.category == category && MatchesFilter(fd, filter)) {
                matchCount++;
            }
        }
        
        if (matchCount == 0) {
            continue; // Skip empty categories
        }

        if (ImGui::TreeNode(category.c_str())) {
            for (const auto& fd : catalog) {
                if (fd.category != category || !MatchesFilter(fd, filter)) {
                    continue;
                }

                ProtocolField* pf = FindField(def, fd.id);
                bool isEnabled = (pf != nullptr && pf->enabled);

                ImGui::PushID(fd.id.c_str());

                // Enable/disable checkbox
                if (ImGui::Checkbox("##enabled", &isEnabled)) {
                    if (isEnabled && !pf) {
                        // Add new field
                        ProtocolField newField;
                        newField.fieldId = fd.id;
                        newField.oscPath = fd.defaultOscPath;
                        newField.wsKey = fd.defaultWsKey;
                        newField.enabled = true;
                        def.fields.push_back(newField);
                    } else if (pf) {
                        // Toggle existing field
                        pf->enabled = isEnabled;
                    }
                    pendingSave = true;
                }

                ImGui::SameLine();
                ImGui::TextUnformatted(fd.label.c_str());

                // Show override controls if enabled
                if (isEnabled && pf) {
                    ImGui::Indent();
                    
                    if (isOsc) {
                        char pathBuffer[128];
                        std::strncpy(pathBuffer, pf->oscPath.c_str(), sizeof(pathBuffer));
                        ImGui::SetNextItemWidth(200);
                        if (ImGui::InputText("OSC Path", pathBuffer, sizeof(pathBuffer))) {
                            pf->oscPath = pathBuffer;
                            pendingSave = true;
                        }
                    } else {
                        char keyBuffer[128];
                        std::strncpy(keyBuffer, pf->wsKey.c_str(), sizeof(keyBuffer));
                        ImGui::SetNextItemWidth(200);
                        if (ImGui::InputText("WS Key", keyBuffer, sizeof(keyBuffer))) {
                            pf->wsKey = keyBuffer;
                            pendingSave = true;
                        }
                    }
                    
                    ImGui::Unindent();
                }

                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal Dialogs
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawNewProtocolModal() {
    if (s_showNewModal) {
        ImGui::OpenPopup("New Protocol##modal");
        s_showNewModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("New Protocol##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", s_newName, sizeof(s_newName));

        const char* transports[] = {"OSC", "WebSocket"};
        ImGui::Combo("Transport", &s_newTransport, transports, 2);

        const char* directions[] = {"Output", "Input"};
        ImGui::Combo("Direction", &s_newDirection, directions, 2);

        // Preset selection
        auto& presets = ProtocolRegistry::GetInstance().GetPresets();
        std::vector<const char*> presetNames = {"None"};
        for (const auto& preset : presets) {
            presetNames.push_back(preset.name.c_str());
        }
        ImGui::Combo("Preset", &s_newPresetIdx, presetNames.data(), (int)presetNames.size());

        ImGui::Separator();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            ProtocolTransport transport = (s_newTransport == 0) ? 
                ProtocolTransport::OSC : ProtocolTransport::WebSocket;
            ProtocolDirection direction = (s_newDirection == 0) ? 
                ProtocolDirection::Output : ProtocolDirection::Input;

            std::string newId = ProtocolRegistry::GetInstance().CreateDefinition(
                s_newName, transport, direction);

            // Apply preset if selected
            if (s_newPresetIdx > 0 && s_newPresetIdx <= (int)presets.size()) {
                auto* newDef = ProtocolRegistry::GetInstance().FindById(newId);
                if (newDef) {
                    const auto& preset = presets[s_newPresetIdx - 1];
                    const auto& catalog = (direction == ProtocolDirection::Output) ?
                        ProtocolRegistry::GetInstance().GetOutputFields() :
                        ProtocolRegistry::GetInstance().GetInputFields();

                    for (const auto& fieldId : preset.fieldIds) {
                        // Find field descriptor
                        const FieldDescriptor* fd = nullptr;
                        for (const auto& desc : catalog) {
                            if (desc.id == fieldId) {
                                fd = &desc;
                                break;
                            }
                        }

                        if (fd) {
                            ProtocolField pf;
                            pf.fieldId = fd->id;
                            pf.oscPath = fd->defaultOscPath;
                            pf.wsKey = fd->defaultWsKey;
                            pf.enabled = true;
                            newDef->fields.push_back(pf);
                        }
                    }
                    ProtocolRegistry::GetInstance().SaveDefinition(*newDef);
                }
            }

            // Select the new protocol
            auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
            for (int i = 0; i < (int)defs.size(); ++i) {
                if (defs[i].id == newId) {
                    s_selectedIndex = i;
                    break;
                }
            }

            ImGui::CloseCurrentPopup();
        }
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
        ImGui::InputText("New Name", s_dupName, sizeof(s_dupName));

        const char* transports[] = {"OSC", "WebSocket"};
        ImGui::Combo("Transport", &s_dupTransport, transports, 2);

        ImGui::Separator();

        if (ImGui::Button("Duplicate", ImVec2(120, 0))) {
            ProtocolTransport transport = (s_dupTransport == 0) ? 
                ProtocolTransport::OSC : ProtocolTransport::WebSocket;

            std::string newId = ProtocolRegistry::GetInstance().DuplicateDefinition(
                s_dupSourceId, s_dupName, transport);

            // Select the new protocol
            auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
            for (int i = 0; i < (int)defs.size(); ++i) {
                if (defs[i].id == newId) {
                    s_selectedIndex = i;
                    break;
                }
            }

            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
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
        ImGui::Text("Rename a category across all fields");

        // Collect existing categories
        std::vector<std::string> categories;
        auto& registry = ProtocolRegistry::GetInstance();
        
        for (const auto& field : registry.GetOutputFields()) {
            if (std::find(categories.begin(), categories.end(), field.category) == categories.end()) {
                categories.push_back(field.category);
            }
        }
        for (const auto& field : registry.GetInputFields()) {
            if (std::find(categories.begin(), categories.end(), field.category) == categories.end()) {
                categories.push_back(field.category);
            }
        }
        std::sort(categories.begin(), categories.end());

        // Category selector
        if (ImGui::BeginCombo("Old Name", s_renCatOldName[0] ? s_renCatOldName : "Select...")) {
            for (const auto& category : categories) {
                if (ImGui::Selectable(category.c_str())) {
                    std::strncpy(s_renCatOldName, category.c_str(), sizeof(s_renCatOldName));
                    s_renCatOldName[sizeof(s_renCatOldName) - 1] = '\0';
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("New Name", s_renCatNewName, sizeof(s_renCatNewName));

        ImGui::Separator();

        bool canRename = (s_renCatOldName[0] != '\0' && s_renCatNewName[0] != '\0');
        if (!canRename) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Rename", ImVec2(120, 0))) {
            // Rename category in all fields
            // Note: const_cast is used here because ProtocolRegistry doesn't provide a non-const accessor
            auto& outFields = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            for (auto& field : outFields) {
                if (field.category == s_renCatOldName) {
                    field.category = s_renCatNewName;
                }
            }

            auto& inFields = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            for (auto& field : inFields) {
                if (field.category == s_renCatOldName) {
                    field.category = s_renCatNewName;
                }
            }

            registry.SaveFieldCatalog();
            ImGui::CloseCurrentPopup();
        }

        if (!canRename) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawDeleteCategoryModal() {
    if (s_showDeleteCatModal) {
        ImGui::OpenPopup("Delete Category##modal");
        s_showDeleteCatModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Delete Category##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete a category and all its fields");
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Warning: This cannot be undone!");

        // Collect existing categories
        std::vector<std::string> categories;
        auto& registry = ProtocolRegistry::GetInstance();
        
        for (const auto& field : registry.GetOutputFields()) {
            if (std::find(categories.begin(), categories.end(), field.category) == categories.end()) {
                categories.push_back(field.category);
            }
        }
        for (const auto& field : registry.GetInputFields()) {
            if (std::find(categories.begin(), categories.end(), field.category) == categories.end()) {
                categories.push_back(field.category);
            }
        }
        std::sort(categories.begin(), categories.end());

        // Category selector
        if (ImGui::BeginCombo("Category", s_delCatName[0] ? s_delCatName : "Select...")) {
            for (const auto& category : categories) {
                if (ImGui::Selectable(category.c_str())) {
                    std::strncpy(s_delCatName, category.c_str(), sizeof(s_delCatName));
                    s_delCatName[sizeof(s_delCatName) - 1] = '\0';
                }
            }
            ImGui::EndCombo();
        }

        // Show field count for selected category
        if (s_delCatName[0] != '\0') {
            int fieldCount = 0;
            for (const auto& field : registry.GetOutputFields()) {
                if (field.category == s_delCatName) {
                    fieldCount++;
                }
            }
            for (const auto& field : registry.GetInputFields()) {
                if (field.category == s_delCatName) {
                    fieldCount++;
                }
            }
            ImGui::Text("This will delete %d field(s)", fieldCount);
        }

        ImGui::Separator();

        bool canDelete = (s_delCatName[0] != '\0');
        if (!canDelete) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            // Delete all fields in this category
            // Note: const_cast is used here because ProtocolRegistry doesn't provide a non-const accessor
            auto& outFields = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            outFields.erase(
                std::remove_if(outFields.begin(), outFields.end(),
                    [](const FieldDescriptor& fd) { return fd.category == s_delCatName; }),
                outFields.end()
            );

            auto& inFields = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            inFields.erase(
                std::remove_if(inFields.begin(), inFields.end(),
                    [](const FieldDescriptor& fd) { return fd.category == s_delCatName; }),
                inFields.end()
            );

            registry.SaveFieldCatalog();
            ImGui::CloseCurrentPopup();
        }

        if (!canDelete) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

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
                // Auto-generate ID from label
                std::string slug;
                for (char* p = s_cfLabel; *p; ++p) {
                    char c = *p;
                    if (std::isalnum((unsigned char)c)) {
                        slug += (char)std::tolower((unsigned char)c);
                    } else if (c == ' ' || c == '-' || c == '_') {
                        if (!slug.empty() && slug.back() != '_') {
                            slug += '_';
                        }
                    }
                }
                if (slug.length() >= sizeof(s_cfId)) {
                    slug.resize(sizeof(s_cfId) - 1);
                }
                std::strncpy(s_cfId, slug.c_str(), sizeof(s_cfId));
                idChanged = true;
            }
        }

        if (idChanged) {
            std::snprintf(s_cfOsc, sizeof(s_cfOsc), "/custom/%s", s_cfId);
            std::snprintf(s_cfWs, sizeof(s_cfWs), "custom_%s", s_cfId);
        }

        // Collect existing categories for dropdown
        std::vector<std::string> categories;
        auto& registry = ProtocolRegistry::GetInstance();
        
        for (const auto& field : registry.GetOutputFields()) {
            if (std::find(categories.begin(), categories.end(), field.category) == categories.end()) {
                categories.push_back(field.category);
            }
        }
        for (const auto& field : registry.GetInputFields()) {
            if (std::find(categories.begin(), categories.end(), field.category) == categories.end()) {
                categories.push_back(field.category);
            }
        }
        std::sort(categories.begin(), categories.end());

        // Category input with dropdown
        float buttonSize = ImGui::GetFrameHeight();
        float itemWidth = ImGui::CalcItemWidth();
        ImGui::SetNextItemWidth(itemWidth - buttonSize - ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::InputText("##Category", s_cfCategory, sizeof(s_cfCategory));
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(buttonSize);
        if (ImGui::BeginCombo("Category", nullptr, ImGuiComboFlags_NoPreview)) {
            for (const auto& category : categories) {
                if (ImGui::Selectable(category.c_str())) {
                    std::strncpy(s_cfCategory, category.c_str(), sizeof(s_cfCategory));
                    s_cfCategory[sizeof(s_cfCategory) - 1] = '\0';
                }
            }
            ImGui::EndCombo();
        }

        const char* types[] = {"Analog Axis", "Digital Button"};
        ImGui::Combo("Type", &s_cfType, types, 2);
        
        ImGui::InputText("Default OSC Path", s_cfOsc, sizeof(s_cfOsc));
        ImGui::InputText("Default WS Key", s_cfWs, sizeof(s_cfWs));

        ImGui::Separator();

        // Check if ID already exists
        bool idExists = false;
        for (const auto& field : registry.GetOutputFields()) {
            if (field.id == s_cfId) {
                idExists = true;
                break;
            }
        }
        if (!idExists) {
            for (const auto& field : registry.GetInputFields()) {
                if (field.id == s_cfId) {
                    idExists = true;
                    break;
                }
            }
        }
        
        if (idExists) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "ID already exists!");
        }

        bool canSave = (s_cfId[0] != '\0') && !idExists;
        if (!canSave) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Save Field", ImVec2(120, 0))) {
            FieldDescriptor fd;
            fd.id = s_cfId;
            fd.label = s_cfLabel;
            fd.category = s_cfCategory;
            fd.type = (s_cfType == 1) ? FieldType::DigitalButton : FieldType::AnalogAxis;
            fd.defaultOscPath = s_cfOsc;
            fd.defaultWsKey = s_cfWs;
            fd.isBuiltIn = false;

            registry.AddOutputField(fd);
            ImGui::CloseCurrentPopup();
        }

        if (!canSave) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawExportProtocolModal() {
    if (s_showExportModal) {
        // Try native dialog first
        if (TryNativeFileDialog(true, s_exportPath)) {
            ProtocolRegistry::GetInstance().ExportDefinition(s_exportId, s_exportPath);
            s_showExportModal = false;
            return;
        }
        
        // Fall back to ImGui browser
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
            SaveSettings();
        }

        ImGui::Separator();
        if (ImGui::Button("Export", ImVec2(120, 0))) {
            ProtocolRegistry::GetInstance().ExportDefinition(s_exportId, s_exportPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawImportProtocolModal() {
    if (s_showImportModal) {
        // Try native dialog first
        std::string importPath;
        if (TryNativeFileDialog(false, importPath)) {
            std::string id = ProtocolRegistry::GetInstance().ImportDefinition(importPath);
            auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
            for (int i = 0; i < (int)defs.size(); ++i) {
                if (defs[i].id == id) {
                    s_selectedIndex = i;
                    break;
                }
            }
            s_showImportModal = false;
            return;
        }
        
        // Fall back to ImGui browser
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
            SaveSettings();
        }

        ImGui::Separator();
        if (ImGui::Button("Import", ImVec2(120, 0))) {
            std::string id = ProtocolRegistry::GetInstance().ImportDefinition(s_importPath);
            auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
            for (int i = 0; i < (int)defs.size(); ++i) {
                if (defs[i].id == id) {
                    s_selectedIndex = i;
                    break;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
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
            auto& registry = ProtocolRegistry::GetInstance();
            auto& definitions = registry.GetDefinitions();
            
            if (s_selectedIndex >= 0 && s_selectedIndex < (int)definitions.size()) {
                const auto& definition = definitions[s_selectedIndex];
                std::vector<std::string> fields;
                
                for (const auto& field : definition.fields) {
                    if (field.enabled) {
                        fields.push_back(field.fieldId);
                    }
                }
                
                registry.SavePreset(s_presetName, fields);
            }
            
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}
