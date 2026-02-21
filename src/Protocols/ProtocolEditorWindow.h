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
 *
 * All changes are persisted immediately to the protocols/definitions/ folder
 * via ProtocolRegistry.
 */
#include <string>
#include <vector>
#include "ProtocolDefinition.h"

class ProtocolEditorWindow {
public:
    static void Draw(bool& open);

private:
    // Left-panel list
    static void DrawProtocolList();

    // Right-panel editor
    static void DrawEditor();
    static void DrawOutputFieldPicker();
    static void DrawInputFieldPicker();
    static void DrawFieldTable(ProtocolDefinition& def,
                               const std::vector<FieldDescriptor>& catalog,
                               bool isOsc,
                               const char* filter,
                               bool& pendingSave);
    static void DrawNewProtocolModal();
    static void DrawDuplicateProtocolModal();
    static void DrawCreateFieldModal();
    static void DrawSavePresetModal();
    static void DrawExportProtocolModal();
    static void DrawImportProtocolModal();
    static void DrawRenameCategoryModal();

    // State shared between sub-sections
    static inline int  s_selectedIndex    = -1;
    static inline bool s_showNewModal     = false;
    static inline bool s_pendingSave      = false;

    // "New protocol" modal state
    static inline char  s_newName[128]     = "New Protocol";
    static inline int   s_newTransport     = 0; // 0=OSC, 1=WebSocket
    static inline int   s_newDirection     = 0; // 0=Output, 1=Input
    static inline int   s_newPresetIdx     = 0; // 0=None

    // "Duplicate protocol" modal state
    static inline bool  s_showDupModal     = false;
    static inline char  s_dupName[128]     = "";
    static inline int   s_dupTransport     = 0;
    static inline std::string s_dupSourceId;

    // "Create field" modal state
    static inline bool  s_showCreateFieldModal = false;
    static inline char  s_cfId[64] = "";
    static inline char  s_cfLabel[64] = "";
    static inline char  s_cfCategory[64] = "Custom";
    static inline int   s_cfType = 0; // 0=Analog, 1=Digital
    static inline char  s_cfOsc[128] = "/custom/";
    static inline char  s_cfWs[128] = "custom_";

    // "Rename category" modal state
    static inline bool  s_showRenameCatModal = false;
    static inline char  s_renCatOldName[64] = "";
    static inline char  s_renCatNewName[64] = "";

    // "Save preset" modal state
    static inline bool  s_showSavePresetModal = false;
    static inline char  s_presetName[128] = "New Preset";

    // "Export protocol" modal state
    static inline bool  s_showExportModal = false;
    static inline char  s_exportPath[256] = "";
    static inline std::string s_exportId;

    // "Import protocol" modal state
    static inline bool  s_showImportModal = false;
    static inline char  s_importPath[256] = "";

    // Filter / search state
    static inline char  s_fieldFilter[128] = {};
};
