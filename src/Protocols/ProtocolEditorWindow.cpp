#include "App/Log.h"
#include "ProtocolEditorWindow.h"
#include "ProtocolRegistry.h"
#include "ProtocolDefinition.h"
#include "../Core/UndoRedo.h"
#include "../Core/BackupManager.h"
#include "../Core/ProtocolValidator.h"
#include "UI/IconsFontAwesome6.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_set>
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
        LOG_ERROR("ProtocolEditor", "Failed to load protocol editor settings: %s", e.what());
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
        LOG_ERROR("ProtocolEditor", "Failed to save protocol editor settings: %s", e.what());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// File Browser (ImGui Fallback) — Windows Explorer-style
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Format a file size in bytes into a human-readable string (KB / MB / GB).
 */
static std::string FormatFileSize(uintmax_t bytes) {
    if (bytes < 1024)
        return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024)
        return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024ull * 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / (1024ull * 1024 * 1024)) + " GB";
}

/**
 * Format a filesystem time_point as "MM/DD/YYYY HH:MM AM/PM".
 */
static std::string FormatFileTime(fs::file_time_type ftime) {
    try {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
        std::tm* tm_info = std::localtime(&tt);
        if (!tm_info) return "";
        char buf[32];
        std::strftime(buf, sizeof(buf), "%m/%d/%Y %I:%M %p", tm_info);
        return buf;
    } catch (...) {
        return "";
    }
}

/**
 * Return a short type label for a file entry (e.g. "File Folder", "JSON File").
 */
static std::string GetFileTypeLabel(const fs::directory_entry& entry) {
    if (entry.is_directory()) return "File Folder";
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".json") return "JSON File";
    if (ext == ".txt")  return "Text File";
    if (ext == ".xml")  return "XML File";
    if (ext == ".csv")  return "CSV File";
    if (ext == ".lua")  return "Lua File";
    if (ext == ".cfg" || ext == ".ini") return "Config File";
    if (!ext.empty()) return ext.substr(1) + " File"; // e.g. ".xyz" -> "XYZ File"
    return "File";
}

/**
 * Returns a list of { label, path } quick-access locations.
 */
static std::vector<std::pair<std::string, std::string>> GetQuickAccessPaths() {
    std::vector<std::pair<std::string, std::string>> places;

#ifdef _WIN32
    auto addEnv = [&](const char* env, const std::string& label, const std::string& sub = "") {
        const char* val = std::getenv(env);
        if (val) {
            std::string p = val;
            if (!sub.empty()) p += "\\" + sub;
            if (fs::is_directory(p)) places.push_back({label, p});
        }
    };
    addEnv("USERPROFILE", "Home");
    addEnv("USERPROFILE", "Desktop",   "Desktop");
    addEnv("USERPROFILE", "Documents", "Documents");
    addEnv("USERPROFILE", "Downloads", "Downloads");
    // Drive roots
    for (char drv = 'C'; drv <= 'Z'; ++drv) {
        std::string root = {drv, ':', '\\'};
        if (fs::is_directory(root)) places.push_back({std::string(1, drv) + ":", root});
    }
#else
    auto addSub = [&](const std::string& base, const std::string& label, const std::string& sub = "") {
        std::string p = sub.empty() ? base : (base + "/" + sub);
        if (fs::is_directory(p)) places.push_back({label, p});
    };
    const char* home = std::getenv("HOME");
    if (home) {
        addSub(home, "Home");
        addSub(home, "Desktop",   "Desktop");
        addSub(home, "Documents", "Documents");
        addSub(home, "Downloads", "Downloads");
    }
    addSub("/", "Root (/)");
#endif

    return places;
}

/**
 * Draw a Windows Explorer-style ImGui file browser.
 *
 * Layout:
 *   [← Back] [→ Fwd] [↑ Up]  [ Address bar (click to edit) ]  [🔍 Search]
 *   ┌─────────────┬──────────────────────────────────────────────────────┐
 *   │ Quick       │  Name ▲  │  Date Modified     │  Type       │ Size  │
 *   │ Access      │──────────────────────────────────────────────────────│
 *   │  Home       │  📁 folder                                            │
 *   │  Desktop    │  📄 file.json                                         │
 *   │  Documents  │                                                       │
 *   │  Downloads  │                                                       │
 *   └─────────────┴──────────────────────────────────────────────────────┘
 *
 * @param currentDir  In/out: current directory
 * @param pathBuf     Buffer to write the selected file path into
 * @param pathBufSize Size of pathBuf
 * @return true if the directory was changed (caller should persist settings)
 */
bool ProtocolEditorWindow::DrawFileBrowser(std::string& currentDir,
                                            char* pathBuf,
                                            size_t pathBufSize) {
    bool directoryChanged = false;

    // Resolve the canonical current path (best-effort)
    std::error_code ec;
    fs::path curPath = fs::weakly_canonical(fs::path(currentDir), ec);
    if (ec || !fs::is_directory(curPath)) {
        curPath = fs::current_path();
        currentDir = curPath.string();
    }

    // ── Helper: navigate to a new directory ─────────────────────────────────
    auto navigateTo = [&](const std::string& newDir) {
        std::error_code ec2;
        fs::path p = fs::weakly_canonical(newDir, ec2);
        if (!ec2 && fs::is_directory(p) && p.string() != curPath.string()) {
            s_fbBackHistory.push_back(currentDir);
            if (s_fbBackHistory.size() > 50) s_fbBackHistory.erase(s_fbBackHistory.begin());
            s_fbForwardHistory.clear();
            currentDir = p.string();
            curPath    = p;
            directoryChanged = true;
            s_fbSelectedEntry.clear();
        }
    };

    // ── Toolbar ──────────────────────────────────────────────────────────────
    // Back
    bool canBack = !s_fbBackHistory.empty();
    if (!canBack) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##fb_back", ImGuiDir_Left)) {
        std::string prev = s_fbBackHistory.back();
        s_fbBackHistory.pop_back();
        s_fbForwardHistory.push_back(currentDir);
        currentDir = prev;
        curPath    = fs::weakly_canonical(prev, ec);
        directoryChanged = true;
        s_fbSelectedEntry.clear();
    }
    if (!canBack) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back");

    ImGui::SameLine(0, 2);

    // Forward
    bool canFwd = !s_fbForwardHistory.empty();
    if (!canFwd) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##fb_fwd", ImGuiDir_Right)) {
        std::string next = s_fbForwardHistory.back();
        s_fbForwardHistory.pop_back();
        s_fbBackHistory.push_back(currentDir);
        currentDir = next;
        curPath    = fs::weakly_canonical(next, ec);
        directoryChanged = true;
        s_fbSelectedEntry.clear();
    }
    if (!canFwd) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forward");

    ImGui::SameLine(0, 2);

    // Up
    bool canUp = (curPath != curPath.root_path());
    if (!canUp) ImGui::BeginDisabled();
    if (ImGui::ArrowButton("##fb_up", ImGuiDir_Up)) {
        navigateTo(curPath.parent_path().string());
    }
    if (!canUp) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up one level");

    ImGui::SameLine(0, 6);

    // Address bar
    const float searchWidth = 160.0f;
    const float spacing     = ImGui::GetStyle().ItemSpacing.x;
    float addrWidth = ImGui::GetContentRegionAvail().x - searchWidth - spacing;

    if (s_fbPathEditMode) {
        // Text-edit mode: user typing a path
        ImGui::SetNextItemWidth(addrWidth);
        bool entered = ImGui::InputText("##fb_addredit", s_fbPathEdit, sizeof(s_fbPathEdit),
                                        ImGuiInputTextFlags_EnterReturnsTrue |
                                        ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemActivated()) ImGui::SetKeyboardFocusHere(-1);
        if (entered) {
            navigateTo(std::string(s_fbPathEdit));
            s_fbPathEditMode = false;
        } else if (ImGui::IsItemDeactivated()) {
            s_fbPathEditMode = false; // Esc or click elsewhere
        }
    } else {
        // Breadcrumb display: split path and render clickable segments
        ImGui::PushStyleColor(ImGuiCol_Button,        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, 0));

        // Collect path components
        std::vector<fs::path> crumbs;
        for (auto it = curPath.begin(); it != curPath.end(); ++it) crumbs.push_back(*it);

        // Build cumulative paths for each crumb
        float usedWidth = 0.0f;
        for (int ci = 0; ci < (int)crumbs.size(); ++ci) {
            std::string label   = crumbs[ci].string();
            if (label == "/") label = "/";
            std::string btnLabel = " " + label + " ##crumb" + std::to_string(ci);
            float btnW = ImGui::CalcTextSize((" " + label + " ").c_str()).x
                         + ImGui::GetStyle().FramePadding.x * 2;

            if (usedWidth + btnW > addrWidth) break; // clip overflow

            if (ImGui::Button(btnLabel.c_str())) {
                // Rebuild path up to this crumb
                fs::path dest;
                for (int j = 0; j <= ci; ++j) dest /= crumbs[j];
                navigateTo(dest.string());
                s_fbPathEditMode = false;
            }
            usedWidth += btnW;

            // Separator chevron between crumbs (skip after last)
            if (ci + 1 < (int)crumbs.size()) {
                ImGui::SameLine(0, 0);
                ImGui::TextDisabled(">");
                ImGui::SameLine(0, 0);
                usedWidth += ImGui::CalcTextSize(">").x;
            }

            if (ci + 1 < (int)crumbs.size()) ImGui::SameLine(0, 0);
        }

        // Invisible button overlapping the whole address area (click -> edit mode)
        ImVec2 crumbEnd = ImGui::GetItemRectMax();
        ImVec2 barTL    = ImGui::GetCursorScreenPos();
        barTL.y        -= ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        float remaining = addrWidth - usedWidth;
        if (remaining > 0) {
            ImGui::SameLine(0, 0);
            ImGui::InvisibleButton("##fb_addrclick", ImVec2(remaining, ImGui::GetFrameHeight()));
            if (ImGui::IsItemClicked()) {
                s_fbPathEditMode = true;
                std::strncpy(s_fbPathEdit, currentDir.c_str(), sizeof(s_fbPathEdit));
                s_fbPathEdit[sizeof(s_fbPathEdit) - 1] = '\0';
            }
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }

    ImGui::SameLine(0, spacing);

    // Search box
    ImGui::SetNextItemWidth(searchWidth);
    ImGui::InputTextWithHint("##fb_search", "Search current folder", s_fbSearch, sizeof(s_fbSearch));

    ImGui::Spacing();

    // ── Main content area ────────────────────────────────────────────────────
    // Reserve a fixed height; caller's modal has buttons below us
    float availH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##fb_main_area", ImVec2(0, availH), false, ImGuiWindowFlags_NoScrollbar);

    // Left sidebar: Quick Access
    const float sidebarW = 130.0f;
    ImGui::BeginChild("##fb_sidebar", ImVec2(sidebarW, 0), true);
    ImGui::TextDisabled("Quick Access");
    ImGui::Separator();
    ImGui::Spacing();

    auto quickPaths = GetQuickAccessPaths();
    for (const auto& [label, path] : quickPaths) {
        bool isActive = (fs::weakly_canonical(path, ec).string() == curPath.string());
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        std::string icon = (label == "Home" || label.size() == 2 /* drive */) ? "  " : "  ";
        if (ImGui::Selectable(("  " + label + "##qa").c_str(), isActive, ImGuiSelectableFlags_SpanAllColumns)) {
            navigateTo(path);
        }
        if (isActive) ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: File listing
    ImGui::BeginChild("##fb_filelist", ImVec2(0, 0), true);

    // Collect directory entries
    struct FileEntry {
        std::string   name;
        bool          isDir;
        std::string   dateStr;
        std::string   typeStr;
        std::string   sizeStr;
        uintmax_t     sizeBytes;
        fs::file_time_type ftime;
        fs::path      fullPath;
    };

    std::vector<FileEntry> entries;
    try {
        for (const auto& de : fs::directory_iterator(curPath, ec)) {
            if (ec) break;
            // Skip hidden files (starting with '.')
            std::string fname = de.path().filename().string();
            if (!fname.empty() && fname[0] == '.') continue;

            bool isDir = de.is_directory();
            bool isFile = de.is_regular_file();
            if (!isDir && !isFile) continue;

            // Only show folders and compatible protocol files (.json).
            if (isFile) {
                std::string ext = de.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".json") continue;
            }

            // Apply search filter
            if (s_fbSearch[0] != '\0') {
                std::string lname = fname, lsearch = s_fbSearch;
                std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                std::transform(lsearch.begin(), lsearch.end(), lsearch.begin(), ::tolower);
                if (lname.find(lsearch) == std::string::npos) continue;
            }

            FileEntry fe;
            fe.name     = fname;
            fe.isDir    = isDir;
            fe.fullPath = de.path();
            fe.typeStr  = GetFileTypeLabel(de);

            std::error_code ecTime;
            fe.ftime    = de.last_write_time(ecTime);
            fe.dateStr  = ecTime ? "" : FormatFileTime(fe.ftime);

            if (isFile) {
                std::error_code ecSz;
                fe.sizeBytes = de.file_size(ecSz);
                fe.sizeStr   = ecSz ? "" : FormatFileSize(fe.sizeBytes);
            } else {
                fe.sizeBytes = 0;
                fe.sizeStr   = "";
            }

            entries.push_back(std::move(fe));
        }
    } catch (...) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Cannot read directory.");
        ImGui::EndChild();
        ImGui::EndChild();
        return directoryChanged;
    }

    // Sort entries
    std::stable_sort(entries.begin(), entries.end(), [&](const FileEntry& a, const FileEntry& b) {
        // Directories always first
        if (a.isDir != b.isDir) return a.isDir > b.isDir;

        int cmp = 0;
        switch (s_fbSortCol) {
            case 0: { // Name
                std::string na = a.name, nb = b.name;
                std::transform(na.begin(), na.end(), na.begin(), ::tolower);
                std::transform(nb.begin(), nb.end(), nb.begin(), ::tolower);
                cmp = na < nb ? -1 : (na > nb ? 1 : 0);
                break;
            }
            case 1: cmp = (a.ftime < b.ftime) ? -1 : (a.ftime > b.ftime ? 1 : 0); break; // Date
            case 2: cmp = a.typeStr < b.typeStr ? -1 : (a.typeStr > b.typeStr ? 1 : 0); break; // Type
            case 3: cmp = (a.sizeBytes < b.sizeBytes) ? -1 : (a.sizeBytes > b.sizeBytes ? 1 : 0); break; // Size
            default: break;
        }
        return s_fbSortDesc ? cmp > 0 : cmp < 0;
    });

    // Table
    ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Resizable     |
        ImGuiTableFlags_Sortable      |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("##fb_table", 4, tableFlags)) {
        // Column headers
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name",          ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 2.0f, 0);
        ImGui::TableSetupColumn("Date modified", ImGuiTableColumnFlags_WidthStretch, 1.4f, 1);
        ImGui::TableSetupColumn("Type",          ImGuiTableColumnFlags_WidthStretch, 0.9f, 2);
        ImGui::TableSetupColumn("Size",          ImGuiTableColumnFlags_WidthFixed,   70.0f, 3);
        ImGui::TableHeadersRow();

        // Handle sort specs
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty && specs->SpecsCount > 0) {
                s_fbSortCol  = (int)specs->Specs[0].ColumnUserID;
                s_fbSortDesc = (specs->Specs[0].SortDirection == ImGuiSortDirection_Descending);
                specs->SpecsDirty = false;
            }
        }

        // Rows
        for (const auto& fe : entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            // Icon + name
            const char* icon = fe.isDir ? "[+] " : "    ";
            std::string label = icon + fe.name + "##" + fe.fullPath.string();

            bool isSelected = (s_fbSelectedEntry == fe.fullPath.string());
            ImGuiSelectableFlags selFlags =
                ImGuiSelectableFlags_SpanAllColumns |
                ImGuiSelectableFlags_AllowDoubleClick;

            if (ImGui::Selectable(label.c_str(), isSelected, selFlags, ImVec2(0, 0))) {
                s_fbSelectedEntry = fe.fullPath.string();
                if (fe.isDir) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        navigateTo(fe.fullPath.string());
                    }
                } else {
                    // Single click selects; populate pathBuf immediately
                    std::string absPath = fs::absolute(fe.fullPath).string();
                    std::strncpy(pathBuf, absPath.c_str(), pathBufSize);
                    pathBuf[pathBufSize - 1] = '\0';
                }
            }

            // Tooltip with full path on hover
            if (ImGui::IsItemHovered() && ImGui::GetIO().MousePos.x > 0) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(fe.fullPath.string().c_str());
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(fe.dateStr.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(fe.typeStr.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(fe.sizeStr.c_str());
        }

        ImGui::EndTable();
    }

    // Empty folder message
    if (entries.empty()) {
        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x * 0.3f);
        ImGui::TextDisabled("This folder is empty.");
    }

    ImGui::EndChild(); // fb_filelist
    ImGui::EndChild(); // fb_main_area

    return directoryChanged;
}

/**
 * Try to use native file dialog if available.
 *
 * @param isSave true for save dialog, false for open dialog
 * @param path Input/output path
 * @return true if native dialog was shown and user selected a file
 */
// ═══════════════════════════════════════════════════════════════════════════
// Main Entry Point
// ═══════════════════════════════════════════════════════════════════════════

// Wrapping button helper: places buttons on the same line as the previous item
// when there is enough room, and falls to the next line when there isn't.
//
//   if (WrapButton("Label")) { /* clicked */ }
//
static bool WrapButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    float buttonW = size.x > 0 ? size.x
                                : ImGui::CalcTextSize(label).x
                                  + ImGui::GetStyle().FramePadding.x * 2.0f;

    // When the cursor is at the start of a new line, GetCursorScreenPos().x +
    // GetContentRegionAvail().x gives the screen-space right edge of the content
    // area. GetContentRegionAvail() is the recommended modern API and accounts
    // for scrollbars and child window sizing correctly.
    float regionRightX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

    // Screen-space X where the next button would end if placed on the same line.
    float nextEndX = ImGui::GetItemRectMax().x
                     + ImGui::GetStyle().ItemSpacing.x
                     + buttonW;

    if (nextEndX <= regionRightX)
        ImGui::SameLine();

    return ImGui::Button(label, size);
}

// Call once at the top of any BeginPopupModal block to close it with ESC.
static void CloseModalOnEscape() {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
}

void ProtocolEditorWindow::DrawContent() {
    if (!s_settingsLoaded) {
        LoadSettings();
        s_settingsLoaded = true;
    }

    // ── Keyboard Shortcuts ───────────────────────────────────────────────────
    if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_Z))
        s_undoManager.Undo();
    if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_Z))
        s_undoManager.Redo();

    // Handle drag-and-drop of .json files onto the window
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_FILE")) {
            const char* filePath = static_cast<const char*>(payload->Data);
            HandleDroppedFile(filePath);
        }
        ImGui::EndDragDropTarget();
    }

    // ── Toolbar ──────────────────────────────────────────────────────────────
    if (WrapButton("+ New Protocol")) {
        s_showNewModal = true;
        std::strncpy(s_newName, "New Protocol", sizeof(s_newName));
        s_newTransport = 0;
        s_newDirection = 0;
    }

    if (WrapButton("Save All")) {
        ProtocolRegistry::GetInstance().SaveAll();
    }

    if (WrapButton("Reload Fields")) {
        ProtocolRegistry::GetInstance().ReloadFieldCatalog();
    }

    if (WrapButton("Import...")) {
        ShowImportDialog();
    }

    if (WrapButton("Export...")) {
        ShowExportDialog();
    }
    
    const char* modName = ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd" : "Ctrl";

    const bool canUndo = s_undoManager.CanUndo();
    if (!canUndo) ImGui::BeginDisabled();
    if (WrapButton("Undo")) s_undoManager.Undo();
    if (!canUndo) ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && canUndo)
        ImGui::SetTooltip("Undo: %s (%s+Z)", s_undoManager.GetUndoDescription().c_str(), modName);

    const bool canRedo = s_undoManager.CanRedo();
    if (!canRedo) ImGui::BeginDisabled();
    if (WrapButton("Redo")) s_undoManager.Redo();
    if (!canRedo) ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && canRedo)
        ImGui::SetTooltip("Redo: %s (%s+Y or %s+Shift+Z)", s_undoManager.GetRedoDescription().c_str(), modName, modName);

    if (WrapButton("Backups...")) s_showBackupModal = true;

    ImGui::Separator();

    // ── Two-column layout: left=list, right=editor ────────────────────────
    static float s_splitWidth = 220.0f;
    const float splitterW  = 6.0f;
    const float totalWidth = ImGui::GetContentRegionAvail().x;

    ImGui::BeginChild("##ProtocolList", ImVec2(s_splitWidth, 0), true);
    DrawProtocolList();
    ImGui::EndChild();

    ImGui::SameLine(0, 0);

    // Draggable splitter handle
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
    ImGui::Button("##hsplit", ImVec2(splitterW, -1));
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemActive())
        s_splitWidth += ImGui::GetIO().MouseDelta.x;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    s_splitWidth = std::clamp(s_splitWidth, 100.0f, totalWidth - splitterW - 100.0f);

    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##ProtocolEditor", ImVec2(0, 0), true);
    DrawEditor();
    ImGui::EndChild();

    // ── Modals ───────────────────────────────────────────────────────────────
    DrawNewProtocolModal();
    DrawDeleteProtocolModal();
    DrawDuplicateProtocolModal();
    DrawCreateFieldModal();
    DrawSavePresetModal();
    DrawLoadPresetModal();
    DrawExportProtocolModal();
    DrawImportProtocolModal();
    DrawRenameCategoryModal();
    DrawDeleteCategoryModal();
    DrawMergeCategoryModal();
    DrawHideCategoryModal();
    DrawSaveTemplateModal();
    DrawLoadTemplateModal();
    DrawValidationResultModal();
    DrawBackupManagerModal();
}

void ProtocolEditorWindow::Draw(bool& open) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Protocols", &open,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    DrawContent();

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
        // Pre-fill with the protocol name so the user only needs to pick a folder.
        // Sanitise to a safe filename (replace spaces and path separators).
        std::string name = defs[s_selectedIndex].name;
        for (char& c : name)
            if (c == ' ' || c == '/' || c == '\\' || c == ':') c = '_';
        std::strncpy(s_exportPath, name.c_str(), sizeof(s_exportPath) - 1);
        s_exportPath[sizeof(s_exportPath) - 1] = '\0';
    }
}

void ProtocolEditorWindow::HandleDroppedFile(const std::string& filePath) {
    if (filePath.empty()) return;

    if (fs::path(filePath).extension() != ".json") {
        s_validationMessage  = "Dropped file must be a JSON file (.json)";
        s_validationIsError  = true;
        s_showValidationModal = true;
        return;
    }

    if (ValidateAndImportProtocol(filePath)) {
        LOG_INFO("ProtocolEditor", "Successfully imported protocol via drag-and-drop: %s", filePath.c_str());
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
                s_showDeleteProtocolModal = true;
                s_deleteProtocolId   = definition.id;
                s_deleteProtocolName = definition.name;
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
    if (WrapButton("+ Create Field")) {
        s_showCreateFieldModal    = true;
        s_cfIsEditing             = false;
        s_cfIsDuplicate           = false;
        s_cfId[0]                 = '\0';
        s_cfLabel[0]              = '\0';
        std::strncpy(s_cfCategory, "Custom", sizeof(s_cfCategory));
        s_cfType                  = 0;
        std::strncpy(s_cfOsc, "/custom/", sizeof(s_cfOsc));
        std::strncpy(s_cfWs,  "custom_",  sizeof(s_cfWs));
        s_cfIdManuallyModified    = false;
        s_cfLabelManuallyModified = false;
        s_cfOscManuallyModified   = false;
        s_cfWsManuallyModified    = false;
    }

    if (WrapButton("Save as Preset")) {
        s_showSavePresetModal = true;
        std::strncpy(s_presetName, "New Preset", sizeof(s_presetName));
    }

    if (WrapButton("Rename Category")) {
        s_showRenameCatModal = true;
        s_renCatOldName[0] = '\0';
        s_renCatNewName[0] = '\0';
    }

    if (WrapButton("Delete Category")) {
        s_showDeleteCatModal = true;
        s_delCatName[0] = '\0';
    }

    if (WrapButton("Merge Categories")) {
        s_showMergeCatModal = true;
        s_mergeSrcCat[0] = '\0';
        s_mergeTgtCat[0] = '\0';
    }

    if (WrapButton("Hide / Show Categories")) {
        s_showHideCatModal = true;
    }

    if (WrapButton("Save as Template")) {
        s_showSaveTemplateModal = true;
        std::strncpy(s_templateName, "New Template", sizeof(s_templateName));
        s_templateDesc[0] = '\0';
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
    if (WrapButton("Rename Category")) {
        s_showRenameCatModal = true;
        s_renCatOldName[0] = '\0';
        s_renCatNewName[0] = '\0';
    }

    if (WrapButton("Delete Category")) {
        s_showDeleteCatModal = true;
        s_delCatName[0] = '\0';
    }

    if (WrapButton("Merge Categories")) {
        s_showMergeCatModal = true;
        s_mergeSrcCat[0] = '\0';
        s_mergeTgtCat[0] = '\0';
    }

    if (WrapButton("Hide / Show Categories")) {
        s_showHideCatModal = true;
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
    auto& registry = ProtocolRegistry::GetInstance();
    for (const auto& fd : catalog) {
        if (std::find(categories.begin(), categories.end(), fd.category) == categories.end()) {
            categories.push_back(fd.category);
        }
    }
    std::sort(categories.begin(), categories.end());

    // Draw categorized fields
    bool fieldDeleted = false;
    for (const auto& category : categories) {
        if (fieldDeleted) break;

        // Skip entire categories hidden for this protocol.
        bool catExcluded = std::find(def.excludedCategories.begin(),
                                     def.excludedCategories.end(),
                                     category) != def.excludedCategories.end();
        if (catExcluded) continue;

        // Count fields in this category that match filter (and are not individually hidden).
        int matchCount = 0;
        for (const auto& fd : catalog) {
            if (fd.category != category || !MatchesFilter(fd, filter)) continue;
            bool fieldExcluded = std::find(def.excludedFieldIds.begin(),
                                           def.excludedFieldIds.end(),
                                           fd.id) != def.excludedFieldIds.end();
            if (!fieldExcluded) matchCount++;
        }

        if (matchCount == 0) {
            continue; // Skip empty categories
        }

        if (ImGui::TreeNode(category.c_str())) {
            for (const auto& fd : catalog) {
                if (fd.category != category || !MatchesFilter(fd, filter)) {
                    continue;
                }
                // Skip fields hidden for this specific protocol.
                if (std::find(def.excludedFieldIds.begin(),
                              def.excludedFieldIds.end(),
                              fd.id) != def.excludedFieldIds.end()) {
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
                // Field type icon: sliders = analog axis, wave-square = digital button
                const bool isDigital = (fd.type == FieldType::DigitalButton);
                ImGui::TextDisabled("%s", isDigital ? ICON_FA_WAVE_SQUARE : ICON_FA_SLIDERS);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", isDigital ? "Digital Button" : "Analog Axis");
                ImGui::SameLine();
                ImGui::TextUnformatted(fd.label.c_str());

                if (!fd.isBuiltIn) {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));

                    // Pre-compute before the button so the result is also
                    // available for the tooltip rendered after the button.
                    bool usedElsewhere = false;
                    for (const auto& d : registry.GetDefinitions()) {
                        if (d.id == def.id) continue;
                        for (const auto& f : d.fields) {
                            if (f.fieldId == fd.id) { usedElsewhere = true; break; }
                        }
                        if (usedElsewhere) break;
                    }

                    if (ImGui::Button("X")) {

                        if (usedElsewhere) {
                            // Field is still used by other protocols so we
                            // cannot remove it from the global catalog.
                            // Instead: remove it from this protocol's active
                            // field list AND add it to the per-protocol
                            // exclusion list so it is hidden from the picker.
                            if (pf) {
                                CreateBackupBeforeOperation("hide field '" + fd.id + "' from protocol");
                                ProtocolField fieldCopy = *pf;
                                std::string defId = def.id;
                                std::string fieldId = fd.id;
                                s_undoManager.ExecuteCommand(std::make_unique<LambdaCommand>(
                                    "Hide field '" + fd.label + "' from protocol",
                                    [&registry, defId, fieldId]() {
                                        if (auto* d = registry.FindById(defId)) {
                                            auto it = std::remove_if(d->fields.begin(), d->fields.end(),
                                                [&](const ProtocolField& f) { return f.fieldId == fieldId; });
                                            if (it != d->fields.end()) {
                                                d->fields.erase(it, d->fields.end());
                                            }
                                            // Hide from this protocol's picker.
                                            if (std::find(d->excludedFieldIds.begin(),
                                                          d->excludedFieldIds.end(),
                                                          fieldId) == d->excludedFieldIds.end()) {
                                                d->excludedFieldIds.push_back(fieldId);
                                            }
                                            registry.SaveDefinition(*d);
                                        }
                                    },
                                    [&registry, defId, fieldId, fieldCopy]() {
                                        if (auto* d = registry.FindById(defId)) {
                                            d->fields.push_back(fieldCopy);
                                            d->excludedFieldIds.erase(
                                                std::remove(d->excludedFieldIds.begin(),
                                                            d->excludedFieldIds.end(), fieldId),
                                                d->excludedFieldIds.end());
                                            registry.SaveDefinition(*d);
                                        }
                                    }
                                ));
                            }
                        } else {
                            CreateBackupBeforeOperation("delete field '" + fd.id + "'");
                            std::vector<RemovedProtocolField> removedFields;
                            for (auto& d : registry.GetDefinitions()) {
                                for (const auto& f : d.fields) {
                                    if (f.fieldId == fd.id) removedFields.push_back({d.id, f});
                                }
                            }
                            ExecuteDeleteField(fd, std::move(removedFields));
                            fieldDeleted = true;
                        }
                    }
                    ImGui::PopStyleColor(3);
                    if (ImGui::IsItemHovered()) {
                        if (usedElsewhere) {
                            ImGui::SetTooltip(
                                "'%s' is still used by other protocols.\n"
                                "It will be hidden from this protocol only.\n"
                                "To delete it entirely, remove it from all other protocols first.",
                                fd.label.c_str());
                        } else {
                            ImGui::SetTooltip(
                                "Delete custom field '%s' and remove it from the catalog.\n"
                                "This cannot be undone once saved.",
                                fd.label.c_str());
                        }
                    }

                    // Edit button
                    ImGui::SameLine();
                    std::string editId = "Edit##edit_" + fd.id;
                    if (ImGui::SmallButton(editId.c_str())) {
                        s_showCreateFieldModal = true;
                        s_cfIsEditing         = true;
                        s_cfIsDuplicate       = false;
                        std::strncpy(s_cfEditingId, fd.id.c_str(),       sizeof(s_cfEditingId));
                        std::strncpy(s_cfId,        fd.id.c_str(),       sizeof(s_cfId));
                        std::strncpy(s_cfLabel,     fd.label.c_str(),    sizeof(s_cfLabel));
                        std::strncpy(s_cfCategory,  fd.category.c_str(), sizeof(s_cfCategory));
                        std::strncpy(s_cfOsc,       fd.defaultOscPath.c_str(), sizeof(s_cfOsc));
                        std::strncpy(s_cfWs,        fd.defaultWsKey.c_str(),   sizeof(s_cfWs));
                        s_cfType                  = (fd.type == FieldType::DigitalButton) ? 1 : 0;
                        s_cfIdManuallyModified    = true;  // Lock ID when editing
                        s_cfLabelManuallyModified = true;
                        s_cfOscManuallyModified   = true;  // Preserve existing paths
                        s_cfWsManuallyModified    = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Edit field '%s'", fd.label.c_str());

                    // Duplicate button
                    ImGui::SameLine();
                    std::string dupId = "Dup##dup_" + fd.id;
                    if (ImGui::SmallButton(dupId.c_str())) {
                        s_showCreateFieldModal = true;
                        s_cfIsEditing         = false;
                        s_cfIsDuplicate       = true;
                        s_cfEditingId[0]      = '\0';
                        // Pre-fill from the source field; the user must give it a new ID.
                        std::string newId = fd.id + "_copy";
                        std::strncpy(s_cfId,       newId.c_str(),        sizeof(s_cfId));
                        std::strncpy(s_cfLabel,    (fd.label + " Copy").c_str(), sizeof(s_cfLabel));
                        std::strncpy(s_cfCategory, fd.category.c_str(),  sizeof(s_cfCategory));
                        std::strncpy(s_cfOsc,      fd.defaultOscPath.c_str(), sizeof(s_cfOsc));
                        std::strncpy(s_cfWs,       fd.defaultWsKey.c_str(),   sizeof(s_cfWs));
                        s_cfType                  = (fd.type == FieldType::DigitalButton) ? 1 : 0;
                        s_cfIdManuallyModified    = true;
                        s_cfLabelManuallyModified = true;
                        s_cfOscManuallyModified   = true;
                        s_cfWsManuallyModified    = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Duplicate field '%s'", fd.label.c_str());
                }

                // Show override controls if enabled
                if (isEnabled && pf) {
                    ImGui::Indent();

                    if (isOsc) {
                        char pathBuffer[128];
                        std::strncpy(pathBuffer, pf->oscPath.c_str(), sizeof(pathBuffer));
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("OSC Path:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##oscPath", pathBuffer, sizeof(pathBuffer))) {
                            pf->oscPath = pathBuffer;
                            pendingSave = true;
                        }
                    } else {
                        char keyBuffer[128];
                        std::strncpy(keyBuffer, pf->wsKey.c_str(), sizeof(keyBuffer));
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("WS Key:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##wsKey", keyBuffer, sizeof(keyBuffer))) {
                            pf->wsKey = keyBuffer;
                            pendingSave = true;
                        }
                    }

                    ImGui::Unindent();
                }

                ImGui::PopID();
                if (fieldDeleted) {
                    break;
                }
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
        CloseModalOnEscape();
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

void ProtocolEditorWindow::DrawDeleteProtocolModal() {
    if (s_showDeleteProtocolModal) {
        ImGui::OpenPopup("Delete Protocol##modal");
        s_showDeleteProtocolModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Delete Protocol##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        CloseModalOnEscape();

        ImGui::Text("Are you sure you want to delete this protocol?");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", s_deleteProtocolName.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("This action cannot be undone.");
        ImGui::Separator();

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            auto& registry = ProtocolRegistry::GetInstance();
            registry.DeleteDefinition(s_deleteProtocolId);
            if (s_selectedIndex >= (int)registry.GetDefinitions().size())
                s_selectedIndex = (int)registry.GetDefinitions().size() - 1;
            s_deleteProtocolId.clear();
            s_deleteProtocolName.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            s_deleteProtocolId.clear();
            s_deleteProtocolName.clear();
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
        CloseModalOnEscape();
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
        CloseModalOnEscape();
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
        CloseModalOnEscape();
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
        // New field — clear everything when not pre-filled by edit/dup buttons.
        if (!s_cfIsEditing && !s_cfIsDuplicate) {
            s_cfId[0] = '\0';
            s_cfLabel[0] = '\0';
            std::strncpy(s_cfCategory, "Custom", sizeof(s_cfCategory));
            s_cfType = 0;
            std::strncpy(s_cfOsc, "/custom/", sizeof(s_cfOsc));
            std::strncpy(s_cfWs,  "custom_",  sizeof(s_cfWs));
            s_cfIdManuallyModified    = false;
            s_cfLabelManuallyModified = false;
            s_cfOscManuallyModified   = false;
            s_cfWsManuallyModified    = false;
        }
    }

    // Helper: turn a string into a safe slug (lower-case, underscores).
    auto slugify = [](const char* src) -> std::string {
        std::string out;
        for (const char* p = src; *p; ++p) {
            char c = *p;
            if (std::isalnum((unsigned char)c))
                out += (char)std::tolower((unsigned char)c);
            else if ((c == ' ' || c == '-' || c == '_') && !out.empty() && out.back() != '_')
                out += '_';
        }
        // Trim trailing underscore
        while (!out.empty() && out.back() == '_') out.pop_back();
        return out;
    };

    // Derive the OSC / WS defaults from the current category slug.
    auto updatePathDefaults = [&]() {
        std::string slug = slugify(s_cfCategory);
        if (slug.empty()) slug = "custom";
        std::string idSlug = slugify(s_cfId);
        if (!s_cfOscManuallyModified)
            std::snprintf(s_cfOsc, sizeof(s_cfOsc), "/%s/%s", slug.c_str(), idSlug.c_str());
        if (!s_cfWsManuallyModified)
            std::snprintf(s_cfWs, sizeof(s_cfWs), "%s_%s", slug.c_str(), idSlug.c_str());
    };

    bool open = true;
    if (ImGui::BeginPopupModal("Create/Edit Field##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        CloseModalOnEscape();
        auto& registry = ProtocolRegistry::GetInstance();

        bool idChanged       = false;
        bool categoryChanged = false;

        // ── ID ────────────────────────────────────────────────────────────
        if (s_cfIsEditing) {
            // ID is immutable when editing an existing field.
            ImGui::BeginDisabled();
            ImGui::InputText("ID", s_cfId, sizeof(s_cfId));
            ImGui::EndDisabled();
        } else {
            if (ImGui::InputText("ID", s_cfId, sizeof(s_cfId))) {
                s_cfIdManuallyModified = true;
                idChanged = true;
                if (!s_cfLabelManuallyModified)
                    std::strncpy(s_cfLabel, s_cfId, sizeof(s_cfLabel));
            }
        }

        // ── Label ─────────────────────────────────────────────────────────
        if (ImGui::InputText("Label", s_cfLabel, sizeof(s_cfLabel))) {
            s_cfLabelManuallyModified = true;
            if (!s_cfIdManuallyModified && !s_cfIsEditing) {
                // Auto-generate ID from label
                std::string slug = slugify(s_cfLabel);
                if (slug.length() >= sizeof(s_cfId)) slug.resize(sizeof(s_cfId) - 1);
                std::strncpy(s_cfId, slug.c_str(), sizeof(s_cfId));
                idChanged = true;
            }
        }

        if (idChanged) updatePathDefaults();

        // ── Category ──────────────────────────────────────────────────────
        std::vector<std::string> categories;
        for (const auto& f : registry.GetOutputFields())
            if (std::find(categories.begin(), categories.end(), f.category) == categories.end())
                categories.push_back(f.category);
        for (const auto& f : registry.GetInputFields())
            if (std::find(categories.begin(), categories.end(), f.category) == categories.end())
                categories.push_back(f.category);
        std::sort(categories.begin(), categories.end());

        float btnSize  = ImGui::GetFrameHeight();
        float itemWidth = ImGui::CalcItemWidth();
        ImGui::SetNextItemWidth(itemWidth - btnSize - ImGui::GetStyle().ItemInnerSpacing.x);
        char prevCat[64];
        std::strncpy(prevCat, s_cfCategory, sizeof(prevCat));
        if (ImGui::InputText("##Category", s_cfCategory, sizeof(s_cfCategory))) {
            categoryChanged = true;
        }
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(btnSize);
        if (ImGui::BeginCombo("Category", nullptr, ImGuiComboFlags_NoPreview)) {
            for (const auto& cat : categories) {
                if (ImGui::Selectable(cat.c_str())) {
                    std::strncpy(s_cfCategory, cat.c_str(), sizeof(s_cfCategory));
                    s_cfCategory[sizeof(s_cfCategory) - 1] = '\0';
                    categoryChanged = true;
                }
            }
            ImGui::EndCombo();
        }
        if (categoryChanged) updatePathDefaults();

        // ── Type ──────────────────────────────────────────────────────────
        const char* types[] = {"Analog Axis", "Digital Button"};
        ImGui::Combo("Type", &s_cfType, types, 2);

        // ── OSC Path ──────────────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Default OSC Path:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##cfOsc", s_cfOsc, sizeof(s_cfOsc)))
            s_cfOscManuallyModified = true;

        // ── WS Key ────────────────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Default WS Key:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##cfWs", s_cfWs, sizeof(s_cfWs)))
            s_cfWsManuallyModified = true;

        ImGui::Separator();

        // ── Validation ────────────────────────────────────────────────────
        bool idExists = false;
        if (!s_cfIsEditing) {
            for (const auto& f : registry.GetOutputFields())
                if (f.id == s_cfId) { idExists = true; break; }
            if (!idExists)
                for (const auto& f : registry.GetInputFields())
                    if (f.id == s_cfId) { idExists = true; break; }
            if (idExists)
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "ID already exists!");
        }

        bool canSave = (s_cfId[0] != '\0') && !idExists;
        if (!canSave) ImGui::BeginDisabled();

        const char* saveLabel = s_cfIsEditing ? "Update Field" : "Save Field";
        if (ImGui::Button(saveLabel, ImVec2(120, 0))) {
            if (s_cfLabel[0] == '\0')
                std::strncpy(s_cfLabel, s_cfId, sizeof(s_cfLabel));

            FieldDescriptor fd;
            fd.id             = s_cfId;
            fd.label          = s_cfLabel;
            fd.category       = s_cfCategory;
            fd.type           = (s_cfType == 1) ? FieldType::DigitalButton : FieldType::AnalogAxis;
            fd.defaultOscPath = s_cfOsc;
            fd.defaultWsKey   = s_cfWs;
            fd.isBuiltIn      = false;

            if (s_cfIsEditing) {
                registry.UpdateOutputField(s_cfEditingId, fd);
            } else {
                registry.AddOutputField(fd);
            }

            // Reset mode flags
            s_cfIsEditing   = false;
            s_cfIsDuplicate = false;
            ImGui::CloseCurrentPopup();
        }

        if (!canSave) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            s_cfIsEditing   = false;
            s_cfIsDuplicate = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawExportProtocolModal() {
    if (s_showExportModal) {
        s_showExportModal = false;
        ImGui::OpenPopup("Export Protocol##modal");
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(780, 540), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Export Protocol##modal", &open)) {
        CloseModalOnEscape();

        float footerH = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y * 2;
        {
            ImGui::BeginChild("##export_browser_area", ImVec2(0, -footerH), false);
            if (DrawFileBrowser(s_exportCurrentDir, s_exportPath, sizeof(s_exportPath))) {
                SaveSettings();
            }
            ImGui::EndChild();
        }

        ImGui::Separator();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("File name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##export_path", s_exportPath, sizeof(s_exportPath));

        float btnW = 110.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btnW * 2 + ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Export", ImVec2(btnW, 0))) {
            // s_exportPath may be just a filename typed by the user (no directory),
            // a full path from clicking an existing file, or empty.
            // Compose the final path: if it has no directory component, prepend
            // the current browser directory.  Append .json if there's no extension.
            std::string finalPath = s_exportPath;
            if (!finalPath.empty()) {
                fs::path p(finalPath);
                if (p.parent_path().empty() || p.parent_path() == fs::path(".")) {
                    finalPath = (fs::path(s_exportCurrentDir) / p).string();
                }
                if (fs::path(finalPath).extension().empty()) {
                    finalPath += ".json";
                }
                ProtocolRegistry::GetInstance().ExportDefinition(s_exportId, finalPath);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(btnW, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ProtocolEditorWindow::DrawImportProtocolModal() {
    if (s_showImportModal) {
        s_showImportModal = false;
        ImGui::OpenPopup("Import Protocol##modal");
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(780, 540), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Import Protocol##modal", &open)) {
        CloseModalOnEscape();

        float footerH = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y * 2;
        {
            ImGui::BeginChild("##import_browser_area", ImVec2(0, -footerH), false);
            if (DrawFileBrowser(s_importCurrentDir, s_importPath, sizeof(s_importPath))) {
                SaveSettings();
            }
            ImGui::EndChild();
        }

        ImGui::Separator();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("File name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##import_path", s_importPath, sizeof(s_importPath));

        float btnW = 110.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btnW * 2 + ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Import", ImVec2(btnW, 0))) {
            if (ValidateAndImportProtocol(s_importPath)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(btnW, 0))) {
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
        CloseModalOnEscape();
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

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions for Category Management
// ═══════════════════════════════════════════════════════════════════════════

std::vector<std::string> ProtocolEditorWindow::GetAllCategories() {
    std::unordered_set<std::string> categorySet;
    auto& registry = ProtocolRegistry::GetInstance();

    for (const auto& field : registry.GetOutputFields())
        categorySet.insert(field.category);
    for (const auto& field : registry.GetInputFields())
        categorySet.insert(field.category);

    std::vector<std::string> categories(categorySet.begin(), categorySet.end());
    std::sort(categories.begin(), categories.end());
    return categories;
}

void ProtocolEditorWindow::CreateBackupBeforeOperation(const std::string& operationName) {
    std::string catalogPath = ProtocolRegistry::GetProtocolsDir() + "/input_fields.json";
    std::string backupPath  = s_backupManager.CreateBackup(catalogPath);
    if (!backupPath.empty())
        LOG_INFO("ProtocolEditor", "Created backup before %s: %s", operationName.c_str(), backupPath.c_str());

    std::string defsBackup = s_backupManager.CreateDirectoryBackup(ProtocolRegistry::GetDefsDir());
    if (!defsBackup.empty())
        LOG_INFO("ProtocolEditor", "Created definitions backup: %s", defsBackup.c_str());
}

bool ProtocolEditorWindow::ValidateAndImportProtocol(const std::string& filePath) {
    ValidationResult result = ProtocolValidator::ValidateProtocolFile(filePath);

    if (!result.IsValid()) {
        s_validationMessage  = result.GetFormattedMessage();
        s_validationIsError  = true;
        s_showValidationModal = true;
        return false;
    }

    if (!result.warnings.empty()) {
        s_validationMessage  = result.GetFormattedMessage();
        s_validationIsError  = false;
        s_showValidationModal = true;
    }

    CreateBackupBeforeOperation("import protocol");

    std::string id = ProtocolRegistry::GetInstance().ImportDefinition(filePath);
    if (id.empty()) {
        s_validationMessage  = "Failed to import protocol.";
        s_validationIsError  = true;
        s_showValidationModal = true;
        return false;
    }

    auto& defs = ProtocolRegistry::GetInstance().GetDefinitions();
    for (int i = 0; i < (int)defs.size(); ++i) {
        if (defs[i].id == id) {
            s_selectedIndex = i;
            break;
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Undo/Redo Command Executors
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::ExecuteDeleteField(const FieldDescriptor& fieldToDelete, std::vector<RemovedProtocolField>&& removedFields) {
    auto& registry = ProtocolRegistry::GetInstance();

    s_undoManager.ExecuteCommand(std::make_unique<LambdaCommand>(
        "Delete field '" + fieldToDelete.label + "'",
        [&registry, id = fieldToDelete.id]() {
            // Delete from catalog
            registry.DeleteOutputField(id);

            // Delete from all definitions
            for (auto& def : registry.GetDefinitions()) {
                auto& fields = def.fields;
                auto it = std::remove_if(fields.begin(), fields.end(),
                    [&](const ProtocolField& pf) { return pf.fieldId == id; });
                if (it != fields.end()) {
                    fields.erase(it, fields.end());
                    registry.SaveDefinition(def);
                }
            }
        },
        [&registry, fieldToDelete, removedFields = std::move(removedFields)]() {
            // Restore to catalog
            registry.AddOutputField(fieldToDelete);

            // Restore to definitions
            for (const auto& rf : removedFields) {
                if (auto* def = registry.FindById(rf.defId)) {
                    def->fields.push_back(rf.field);
                    registry.SaveDefinition(*def);
                }
            }
        }
    ));
}

void ProtocolEditorWindow::ExecuteDeleteCategory(const std::string& categoryName) {
    auto& registry = ProtocolRegistry::GetInstance();

    // Snapshot deleted fields for undo
    std::vector<FieldDescriptor> deletedOut, deletedIn;
    for (const auto& f : registry.GetOutputFields())
        if (f.category == categoryName) deletedOut.push_back(f);
    for (const auto& f : registry.GetInputFields())
        if (f.category == categoryName) deletedIn.push_back(f);

    s_undoManager.ExecuteCommand(std::make_unique<LambdaCommand>(
        "Delete category '" + categoryName + "'",
        [categoryName, &registry]() {
            auto& out = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            out.erase(std::remove_if(out.begin(), out.end(),
                [&](const FieldDescriptor& f){ return f.category == categoryName; }), out.end());
            auto& in  = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            in.erase(std::remove_if(in.begin(), in.end(),
                [&](const FieldDescriptor& f){ return f.category == categoryName; }), in.end());
            registry.SaveFieldCatalog();
        },
        [deletedOut, deletedIn, &registry]() {
            auto& out = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            for (const auto& f : deletedOut) out.push_back(f);
            auto& in  = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            for (const auto& f : deletedIn)  in.push_back(f);
            registry.SaveFieldCatalog();
        }
    ));
}

void ProtocolEditorWindow::ExecuteRenameCategory(const std::string& oldName, const std::string& newName) {
    auto& registry = ProtocolRegistry::GetInstance();

    s_undoManager.ExecuteCommand(std::make_unique<LambdaCommand>(
        "Rename category '" + oldName + "' to '" + newName + "'",
        [oldName, newName, &registry]() {
            auto& out = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            for (auto& f : out) if (f.category == oldName) f.category = newName;
            auto& in  = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            for (auto& f : in)  if (f.category == oldName) f.category = newName;
            registry.SaveFieldCatalog();
        },
        [oldName, newName, &registry]() {
            auto& out = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            for (auto& f : out) if (f.category == newName) f.category = oldName;
            auto& in  = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            for (auto& f : in)  if (f.category == newName) f.category = oldName;
            registry.SaveFieldCatalog();
        }
    ));
}

void ProtocolEditorWindow::ExecuteMergeCategories(const std::string& srcCat, const std::string& tgtCat) {
    auto& registry = ProtocolRegistry::GetInstance();

    // Snapshot which field IDs were in the source for undo
    std::vector<std::string> movedOutIds, movedInIds;
    for (const auto& f : registry.GetOutputFields())
        if (f.category == srcCat) movedOutIds.push_back(f.id);
    for (const auto& f : registry.GetInputFields())
        if (f.category == srcCat) movedInIds.push_back(f.id);

    s_undoManager.ExecuteCommand(std::make_unique<LambdaCommand>(
        "Merge '" + srcCat + "' into '" + tgtCat + "'",
        [srcCat, tgtCat, &registry]() {
            auto& out = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            for (auto& f : out) if (f.category == srcCat) f.category = tgtCat;
            auto& in  = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            for (auto& f : in)  if (f.category == srcCat) f.category = tgtCat;
            registry.SaveFieldCatalog();
        },
        [srcCat, tgtCat, movedOutIds, movedInIds, &registry]() {
            auto& out = const_cast<std::vector<FieldDescriptor>&>(registry.GetOutputFields());
            for (auto& f : out)
                for (const auto& id : movedOutIds)
                    if (f.id == id) { f.category = srcCat; break; }
            auto& in  = const_cast<std::vector<FieldDescriptor>&>(registry.GetInputFields());
            for (auto& f : in)
                for (const auto& id : movedInIds)
                    if (f.id == id) { f.category = srcCat; break; }
            registry.SaveFieldCatalog();
        }
    ));
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal: Load Preset
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawLoadPresetModal() {
    if (s_showLoadPresetModal) {
        ImGui::OpenPopup("Load Preset##modal");
        s_showLoadPresetModal = false;
        s_loadPresetIdx = -1;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Load Preset##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        CloseModalOnEscape();
        ImGui::Text("Apply a preset to the current protocol");
        ImGui::Separator();

        const auto& presets = ProtocolRegistry::GetInstance().GetPresets();
        if (presets.empty()) {
            ImGui::TextDisabled("No presets saved yet. Use \"Save as Preset\" to create one.");
        } else {
            for (int i = 0; i < (int)presets.size(); ++i) {
                bool selected = (s_loadPresetIdx == i);
                if (ImGui::Selectable(presets[i].name.c_str(), selected))
                    s_loadPresetIdx = i;
            }
        }

        ImGui::Separator();
        bool canLoad = (s_loadPresetIdx >= 0 && s_loadPresetIdx < (int)presets.size());
        if (!canLoad) ImGui::BeginDisabled();

        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            auto& registry    = ProtocolRegistry::GetInstance();
            auto& definitions = registry.GetDefinitions();
            if (s_selectedIndex >= 0 && s_selectedIndex < (int)definitions.size()) {
                auto& def          = definitions[s_selectedIndex];
                const auto& preset = presets[s_loadPresetIdx];
                bool isOsc         = (def.transport == ProtocolTransport::OSC);
                const auto& catalog = (def.direction == ProtocolDirection::Output)
                                      ? registry.GetOutputFields()
                                      : registry.GetInputFields();

                for (const auto& fieldId : preset.fieldIds) {
                    // Only add if not already present
                    bool already = false;
                    for (const auto& pf : def.fields)
                        if (pf.fieldId == fieldId) { already = true; break; }
                    if (already) continue;

                    for (const auto& fd : catalog) {
                        if (fd.id == fieldId) {
                            ProtocolField pf;
                            pf.fieldId = fd.id;
                            pf.oscPath = fd.defaultOscPath;
                            pf.wsKey   = fd.defaultWsKey;
                            pf.enabled = true;
                            def.fields.push_back(pf);
                            break;
                        }
                    }
                }
                registry.SaveDefinition(def);
            }
            ImGui::CloseCurrentPopup();
        }

        if (!canLoad) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal: Merge Categories
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawMergeCategoryModal() {
    if (s_showMergeCatModal) {
        ImGui::OpenPopup("Merge Categories##modal");
        s_showMergeCatModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Merge Categories##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        CloseModalOnEscape();
        ImGui::TextWrapped("Move all fields from the source category into the target category, then remove the source.");

        auto categories = GetAllCategories();

        if (ImGui::BeginCombo("Source Category", s_mergeSrcCat[0] ? s_mergeSrcCat : "Select...")) {
            for (const auto& cat : categories)
                if (ImGui::Selectable(cat.c_str()))
                    std::strncpy(s_mergeSrcCat, cat.c_str(), sizeof(s_mergeSrcCat));
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Target Category", s_mergeTgtCat[0] ? s_mergeTgtCat : "Select...")) {
            for (const auto& cat : categories)
                if (ImGui::Selectable(cat.c_str()))
                    std::strncpy(s_mergeTgtCat, cat.c_str(), sizeof(s_mergeTgtCat));
            ImGui::EndCombo();
        }

        // Preview field count being moved
        if (s_mergeSrcCat[0] && s_mergeTgtCat[0] && std::strcmp(s_mergeSrcCat, s_mergeTgtCat) != 0) {
            auto& registry = ProtocolRegistry::GetInstance();
            int count = 0;
            for (const auto& f : registry.GetOutputFields()) if (f.category == s_mergeSrcCat) count++;
            for (const auto& f : registry.GetInputFields())  if (f.category == s_mergeSrcCat) count++;
            ImGui::TextDisabled("%d field(s) will be moved.", count);
        }

        ImGui::Separator();

        bool canMerge = s_mergeSrcCat[0] && s_mergeTgtCat[0] &&
                        std::strcmp(s_mergeSrcCat, s_mergeTgtCat) != 0;
        if (!canMerge) ImGui::BeginDisabled();

        if (ImGui::Button("Merge", ImVec2(120, 0))) {
            CreateBackupBeforeOperation("merge categories");
            ExecuteMergeCategories(s_mergeSrcCat, s_mergeTgtCat);
            ImGui::CloseCurrentPopup();
        }

        if (!canMerge) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal: Hide / Show Categories (per-protocol)
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawHideCategoryModal() {
    if (s_showHideCatModal) {
        ImGui::OpenPopup("Hide / Show Categories##modal");
        s_showHideCatModal = false;
    }

    bool open = true;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Size: at least 320px wide, up to 40% of the main viewport width.
    float vpWidth   = viewport->WorkSize.x;
    float modalW    = std::clamp(vpWidth * 0.40f, 320.0f, 600.0f);

    // Auto-center when opened or when the main window is resized.
    static ImVec2 lastVpSize = { 0.0f, 0.0f };
    bool vpResized = (viewport->WorkSize.x != lastVpSize.x || viewport->WorkSize.y != lastVpSize.y);
    if (vpResized) { lastVpSize = viewport->WorkSize; }
    ImGui::SetNextWindowPos(viewport->GetCenter(), vpResized ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGui::SetNextWindowSizeConstraints(ImVec2(modalW, 0.0f), ImVec2(modalW, FLT_MAX));
    if (ImGui::BeginPopupModal("Hide / Show Categories##modal", &open,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        CloseModalOnEscape();
        auto& registry   = ProtocolRegistry::GetInstance();
        auto& definitions = registry.GetDefinitions();
        if (s_selectedIndex < 0 || s_selectedIndex >= (int)definitions.size()) {
            ImGui::CloseCurrentPopup();
        } else {
            ProtocolDefinition& def = definitions[s_selectedIndex];

            ImGui::TextWrapped(
                "Tick the categories you want to hide from this protocol's field list.\n"
                "Hidden categories are still available to all other protocols.");
            ImGui::Separator();

            // Build the full category list for the relevant catalog.
            bool isOutput = (def.direction == ProtocolDirection::Output);
            const auto& catalog = isOutput ? registry.GetOutputFields()
                                           : registry.GetInputFields();
            std::vector<std::string> cats;
            for (const auto& fd : catalog)
                if (std::find(cats.begin(), cats.end(), fd.category) == cats.end())
                    cats.push_back(fd.category);
            std::sort(cats.begin(), cats.end());

            bool changed = false;
            for (const auto& cat : cats) {
                bool hidden = std::find(def.excludedCategories.begin(),
                                        def.excludedCategories.end(),
                                        cat) != def.excludedCategories.end();
                if (ImGui::Checkbox(cat.c_str(), &hidden)) {
                    if (hidden) {
                        def.excludedCategories.push_back(cat);
                    } else {
                        def.excludedCategories.erase(
                            std::remove(def.excludedCategories.begin(),
                                        def.excludedCategories.end(), cat),
                            def.excludedCategories.end());
                    }
                    changed = true;
                }
            }

            if (changed) registry.SaveDefinition(def);

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal: Save Template
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawSaveTemplateModal() {
    if (s_showSaveTemplateModal) {
        ImGui::OpenPopup("Save Template##modal");
        s_showSaveTemplateModal = false;
    }

    bool open = true;
    if (ImGui::BeginPopupModal("Save Template##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        CloseModalOnEscape();
        ImGui::Text("Save the current protocol as a reusable template.");
        ImGui::Separator();

        ImGui::InputText("Template Name", s_templateName, sizeof(s_templateName));
        ImGui::InputTextMultiline("Description", s_templateDesc, sizeof(s_templateDesc),
                                  ImVec2(360, 60));

        ImGui::Separator();

        bool canSave = s_templateName[0] != '\0';
        if (!canSave) ImGui::BeginDisabled();

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            auto& registry    = ProtocolRegistry::GetInstance();
            auto& definitions = registry.GetDefinitions();

            if (s_selectedIndex >= 0 && s_selectedIndex < (int)definitions.size()) {
                const auto& def = definitions[s_selectedIndex];

                // Templates are saved as exported protocol JSON files under
                // protocols/templates/<name>.json so they can be imported later.
                std::string templatesDir = ProtocolRegistry::GetProtocolsDir() + "/templates";
                std::error_code ec;
                fs::create_directories(templatesDir, ec);

                // Sanitise name into a filename
                std::string filename = s_templateName;
                for (auto& c : filename)
                    if (c == ' ' || c == '/' || c == '\\') c = '_';
                std::string templatePath = templatesDir + "/" + filename + ".json";

                // Write template metadata + definition
                try {
                    json j;
                    j["template_name"]    = s_templateName;
                    j["template_desc"]    = s_templateDesc;
                    j["protocol_id"]      = def.id;
                    j["protocol_name"]    = def.name;
                    j["transport"]        = (def.transport == ProtocolTransport::OSC) ? "OSC" : "WebSocket";
                    j["direction"]        = (def.direction == ProtocolDirection::Output) ? "Output" : "Input";
                    json fieldsArr = json::array();
                    for (const auto& pf : def.fields) {
                        if (!pf.enabled) continue;
                        json fj;
                        fj["fieldId"] = pf.fieldId;
                        fj["oscPath"] = pf.oscPath;
                        fj["wsKey"]   = pf.wsKey;
                        fieldsArr.push_back(fj);
                    }
                    j["fields"] = fieldsArr;

                    std::ofstream out(templatePath);
                    out << j.dump(4);
                    LOG_INFO("ProtocolEditor", "Saved template: %s", templatePath.c_str());
                } catch (const std::exception& e) {
                    LOG_ERROR("ProtocolEditor", "Failed to save template: %s", e.what());
                }
            }
            ImGui::CloseCurrentPopup();
        }

        if (!canSave) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal: Load Template
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawLoadTemplateModal() {
    if (s_showLoadTemplateModal) {
        ImGui::OpenPopup("Load Template##modal");
        s_showLoadTemplateModal = false;
        s_loadTemplateIdx = -1;
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(500, 340), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Load Template##modal", &open)) {
        CloseModalOnEscape();
        // Enumerate template files
        std::string templatesDir = ProtocolRegistry::GetProtocolsDir() + "/templates";
        std::vector<std::string> templatePaths;
        std::vector<std::string> templateNames;
        std::vector<std::string> templateDescs;

        std::error_code ec;
        if (fs::is_directory(templatesDir, ec)) {
            for (const auto& entry : fs::directory_iterator(templatesDir, ec)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".json") continue;
                try {
                    std::ifstream f(entry.path());
                    json j;
                    f >> j;
                    templatePaths.push_back(entry.path().string());
                    templateNames.push_back(j.value("template_name", entry.path().stem().string()));
                    templateDescs.push_back(j.value("template_desc", ""));
                } catch (...) {}
            }
        }

        if (templateNames.empty()) {
            ImGui::TextDisabled("No templates found. Use \"Save Template\" to create one.");
        } else {
            ImGui::BeginChild("##tpl_list", ImVec2(0, -70), true);
            for (int i = 0; i < (int)templateNames.size(); ++i) {
                bool sel = (s_loadTemplateIdx == i);
                if (ImGui::Selectable(templateNames[i].c_str(), sel))
                    s_loadTemplateIdx = i;
                if (sel && !templateDescs[i].empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("  %s", templateDescs[i].c_str());
                }
            }
            ImGui::EndChild();
        }

        ImGui::Separator();
        bool canLoad = s_loadTemplateIdx >= 0 && s_loadTemplateIdx < (int)templatePaths.size();
        if (!canLoad) ImGui::BeginDisabled();

        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            try {
                std::ifstream f(templatePaths[s_loadTemplateIdx]);
                json j;
                f >> j;

                auto& registry    = ProtocolRegistry::GetInstance();
                auto& definitions = registry.GetDefinitions();
                if (s_selectedIndex >= 0 && s_selectedIndex < (int)definitions.size()) {
                    auto& def           = definitions[s_selectedIndex];
                    const auto& catalog = (def.direction == ProtocolDirection::Output)
                                          ? registry.GetOutputFields()
                                          : registry.GetInputFields();

                    for (const auto& fj : j.value("fields", json::array())) {
                        std::string fieldId = fj.value("fieldId", "");
                        if (fieldId.empty()) continue;

                        bool already = false;
                        for (const auto& pf : def.fields)
                            if (pf.fieldId == fieldId) { already = true; break; }
                        if (already) continue;

                        // Find in catalog for defaults, fall back to template values
                        ProtocolField pf;
                        pf.fieldId = fieldId;
                        pf.oscPath = fj.value("oscPath", "");
                        pf.wsKey   = fj.value("wsKey", "");
                        pf.enabled = true;
                        for (const auto& fd : catalog) {
                            if (fd.id == fieldId) {
                                if (pf.oscPath.empty()) pf.oscPath = fd.defaultOscPath;
                                if (pf.wsKey.empty())   pf.wsKey   = fd.defaultWsKey;
                                break;
                            }
                        }
                        def.fields.push_back(pf);
                    }
                    registry.SaveDefinition(def);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolEditor", "Failed to load template: %s", e.what());
            }
            ImGui::CloseCurrentPopup();
        }

        if (!canLoad) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal: Validation Result
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawValidationResultModal() {
    if (s_showValidationModal) {
        ImGui::OpenPopup("Validation Result##modal");
        s_showValidationModal = false;
    }

    bool open = true;
    // SetNextWindowSizeConstraints enforces a minimum width while still
    // allowing AlwaysAutoResize to size the height to the wrapped text.
    ImGui::SetNextWindowSizeConstraints(ImVec2(480, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Validation Result##modal", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        CloseModalOnEscape();
        if (s_validationIsError)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Validation Failed");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "Validation Warnings");

        ImGui::Separator();
        ImGui::TextWrapped("%s", s_validationMessage.c_str());
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Modal: Backup Manager
// ═══════════════════════════════════════════════════════════════════════════

void ProtocolEditorWindow::DrawBackupManagerModal() {
    if (s_showBackupModal) {
        ImGui::OpenPopup("Backup Manager##modal");
        s_showBackupModal = false;
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(400, 220), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Backup Manager##modal", &open)) {
        CloseModalOnEscape();
        ImGui::Text("Automatic Backup System");
        ImGui::Separator();

        size_t backupSize = s_backupManager.GetTotalBackupSize();
        double sizeMB     = backupSize / (1024.0 * 1024.0);
        ImGui::Text("Total backup size: %.2f MB", sizeMB);
        ImGui::Text("Backup directory:  ./backups");

        ImGui::Spacing();
        if (ImGui::Button("Clean Old Backups")) s_backupManager.CleanupOldBackups();
        ImGui::SameLine();
        if (ImGui::Button("Clear All Backups")) s_backupManager.ClearAllBackups();

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}