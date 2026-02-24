#pragma once

/**
 * ProtocolEditorWindow
 *
 * Full-screen ImGui window that lets users:
 *   • Browse the list of defined OSC / WebSocket protocols
 *   • Create new protocol definitions (input or output, OSC or WebSocket)
 *   • Edit name, transport settings (host / port)
 *   • Toggle individual data fields on/off and override their OSC path / WS key
 *   • Delete protocol definitions
 *   • Import/export protocols via native file dialogs or ImGui browser
 *   • Manage categories (rename/delete)
 *
 * All changes are persisted immediately to the protocols/definitions/ folder
 * via ProtocolRegistry.
 */
#include <string>
#include <vector>
#include "ProtocolDefinition.h"

class ProtocolEditorWindow {
public:
    /**
     * Draw the Protocol Editor window.
     * 
     * @param open Window open state (set to false to close)
     */
    static void Draw(bool& open);
    
    /**
     * Trigger import dialog from external code (e.g., main menu).
     */
    static void ShowImportDialog();
    
    /**
     * Trigger export dialog for selected protocol from external code.
     */
    static void ShowExportDialog();

private:
    // ── Left panel ───────────────────────────────────────────────────────────
    static void DrawProtocolList();

    // ── Right panel ──────────────────────────────────────────────────────────
    static void DrawEditor();
    static void DrawOutputFieldPicker();
    static void DrawInputFieldPicker();
    static void DrawFieldTable(ProtocolDefinition& def,
                               const std::vector<FieldDescriptor>& catalog,
                               bool isOsc,
                               const char* filter,
                               bool& pendingSave);

    // ── Modals ───────────────────────────────────────────────────────────────
    static void DrawNewProtocolModal();
    static void DrawDuplicateProtocolModal();
    static void DrawCreateFieldModal();
    static void DrawSavePresetModal();
    static void DrawExportProtocolModal();
    static void DrawImportProtocolModal();
    static void DrawRenameCategoryModal();
    static void DrawDeleteCategoryModal();

    // ── Helper functions ─────────────────────────────────────────────────────
    static void LoadSettings();
    static void SaveSettings();
    static bool DrawFileBrowser(std::string& currentDir, char* pathBuf, size_t pathBufSize);
    static bool TryNativeFileDialog(bool isSave, std::string& path);

    // ── State variables ──────────────────────────────────────────────────────
    
    // Selection and general state
    static inline int  s_selectedIndex    = -1;
    static inline bool s_showNewModal     = false;
    static inline bool s_pendingSave      = false;
    static inline bool s_settingsLoaded   = false;

    // New protocol modal state
    static inline char  s_newName[128]     = "New Protocol";
    static inline int   s_newTransport     = 0; // 0=OSC, 1=WebSocket
    static inline int   s_newDirection     = 0; // 0=Output, 1=Input
    static inline int   s_newPresetIdx     = 0; // 0=None

    // Duplicate protocol modal state
    static inline bool  s_showDupModal     = false;
    static inline char  s_dupName[128]     = "";
    static inline int   s_dupTransport     = 0;
    static inline std::string s_dupSourceId;

    // Create field modal state
    static inline bool  s_showCreateFieldModal = false;
    static inline char  s_cfId[64] = "";
    static inline char  s_cfLabel[64] = "";
    static inline char  s_cfCategory[64] = "Custom";
    static inline int   s_cfType = 0; // 0=Analog, 1=Digital
    static inline char  s_cfOsc[128] = "/custom/";
    static inline char  s_cfWs[128] = "custom_";
    static inline bool  s_cfIdManuallyModified = false;

    // Rename category modal state
    static inline bool  s_showRenameCatModal = false;
    static inline char  s_renCatOldName[64] = "";
    static inline char  s_renCatNewName[64] = "";

    // Delete category modal state
    static inline bool  s_showDeleteCatModal = false;
    static inline char  s_delCatName[64] = "";

    // Save preset modal state
    static inline bool  s_showSavePresetModal = false;
    static inline char  s_presetName[128] = "New Preset";

    // Export protocol modal state
    static inline bool  s_showExportModal = false;
    static inline char  s_exportPath[256] = "";
    static inline std::string s_exportId;
    static inline std::string s_exportCurrentDir = ".";

    // Import protocol modal state
    static inline bool  s_showImportModal = false;
    static inline char  s_importPath[256] = "";
    static inline std::string s_importCurrentDir = ".";

    // Filter / search state
    static inline char  s_fieldFilter[128] = {};
};
