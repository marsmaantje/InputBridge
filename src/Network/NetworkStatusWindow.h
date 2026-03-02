#pragma once

class NetworkStatusWindow {
public:
    /**
     * Draw the Network Server panel.
     * Also hosts the Protocol Editor accessible via the "Protocols" tab.
     */
    static void Draw(int& update_rate, bool& dynamic_rate, float messages_per_second);

private:
    /** Draws the quick-summary + full-editor launcher inside the current window. */
    static void DrawProtocolEditorInline();

    static inline bool s_showFullEditor = false;
};