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
#include "../Core/UndoRedo.h"
#include "../Core/BackupManager.h"
#include "ProtocolDefinition.h"

struct RemovedProtocolField {
    std::string defId;
    ProtocolField field;
};

class ProtocolEditorWindow {
public:
    /**
     * Draw the Protocol Editor window (standalone floating window).
     * @param open Window open state (set to false to close)
     */
    static void Draw(bool& open);

    /**
     * Draw only the Protocol Editor content, no ImGui::Begin/End wrapper.
     * Use this to embed the editor inside an existing window or child region.
     */
    static void DrawContent();

    /**
     * Trigger import dialog from external code (e.g., main menu).
     */
    static void ShowImportDialog();

    /**
     * Trigger export dialog for selected protocol from external code.
     */
    static void ShowExportDialog();

    /**
     * Handle a file path dropped onto the window via drag-and-drop.
     */
    static void HandleDroppedFile(const std::string& filePath);

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
    static void DrawMergeCategoryModal();
    static void DrawSaveTemplateModal();
    static void DrawLoadPresetModal();
    static void DrawLoadTemplateModal();
    static void DrawValidationResultModal();
    static void DrawBackupManagerModal();

    // ── Helper functions ─────────────────────────────────────────────────────
    static void LoadSettings();
    static void SaveSettings();
    static bool DrawFileBrowser(std::string& currentDir, char* pathBuf, size_t pathBufSize);
    static std::vector<std::string> GetAllCategories();
    static void CreateBackupBeforeOperation(const std::string& operationName);
    static bool ValidateAndImportProtocol(const std::string& filePath);

    // ── Undo/Redo command executors ──────────────────────────────────────────
    static void ExecuteDeleteField(const FieldDescriptor& fieldToDelete, std::vector<RemovedProtocolField>&& removedFields);
    static void ExecuteDeleteCategory(const std::string& categoryName);
    static void ExecuteRenameCategory(const std::string& oldName, const std::string& newName);
    static void ExecuteMergeCategories(const std::string& sourceCategory, const std::string& targetCategory);

    // ── State variables ──────────────────────────────────────────────────────

    // Core managers
    static inline UndoRedoManager s_undoManager{50};
    static inline BackupManager   s_backupManager{"./backups", 10};

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
    static inline bool  s_cfLabelManuallyModified = false;

    // Rename category modal state
    static inline bool  s_showRenameCatModal = false;
    static inline char  s_renCatOldName[64] = "";
    static inline char  s_renCatNewName[64] = "";

    // Delete category modal state
    static inline bool  s_showDeleteCatModal = false;
    static inline char  s_delCatName[64] = "";

    // Merge category modal state
    static inline bool  s_showMergeCatModal = false;
    static inline char  s_mergeSrcCat[64]   = "";
    static inline char  s_mergeTgtCat[64]   = "";

    // Validation result modal state
    static inline bool        s_showValidationModal = false;
    static inline std::string s_validationMessage;
    static inline bool        s_validationIsError   = false;

    // Backup manager modal state
    static inline bool s_showBackupModal = false;

    // Save preset modal state
    static inline bool  s_showSavePresetModal = false;
    static inline char  s_presetName[128] = "New Preset";

    // Load preset modal state
    static inline bool  s_showLoadPresetModal = false;
    static inline int   s_loadPresetIdx       = -1;

    // Save/Load template modal state
    static inline bool  s_showSaveTemplateModal = false;
    static inline bool  s_showLoadTemplateModal = false;
    static inline char  s_templateName[128]     = "New Template";
    static inline char  s_templateDesc[256]     = "";
    static inline int   s_loadTemplateIdx       = -1;

    // Export protocol modal state
    static inline bool  s_showExportModal = false;
    static inline char  s_exportPath[256] = "";
    static inline std::string s_exportId;
    static inline std::string s_exportCurrentDir = ".";

    // Import protocol modal state
    static inline bool  s_showImportModal = false;
    static inline char  s_importPath[256] = "";
    static inline std::string s_importCurrentDir = ".";

    // Export protocol modal state (also used by async native dialog)
    // Native dialog async state — the Win32 dialog runs on a worker thread so
    // Filter / search state
    static inline char  s_fieldFilter[128] = {};

    // ── File Browser state (ImGui fallback) ──────────────────────────────────
    static inline std::vector<std::string> s_fbBackHistory;
    static inline std::vector<std::string> s_fbForwardHistory;
    static inline char   s_fbSearch[128]    = {};
    static inline bool   s_fbPathEditMode   = false;
    static inline char   s_fbPathEdit[512]  = {};
    static inline int    s_fbSortCol        = 0;    // 0=Name, 1=Date, 2=Type, 3=Size
    static inline bool   s_fbSortDesc       = false;
    static inline std::string s_fbSelectedEntry;    // highlighted filename
};
