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
    static void DrawNewProtocolModal();

    // State shared between sub-sections
    static inline int  s_selectedIndex    = -1;
    static inline bool s_showNewModal     = false;
    static inline bool s_pendingSave      = false;

    // "New protocol" modal state
    static inline char  s_newName[128]     = "New Protocol";
    static inline int   s_newTransport     = 0; // 0=OSC, 1=WebSocket
    static inline int   s_newDirection     = 0; // 0=Output, 1=Input

    // Filter / search state
    static inline char  s_fieldFilter[128] = {};
};
