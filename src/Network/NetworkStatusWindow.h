#pragma once

class NetworkStatusWindow {
public:
    /**
     * Draw the Network Server panel (with ImGui::Begin/End).
     */
    static void Draw(int& update_rate, bool& dynamic_rate, float messages_per_second);

    /**
     * Draw only the content (no Begin/End) for use inside a child window.
     */
    static void DrawContentOnly(int& update_rate, bool& dynamic_rate, float messages_per_second);
};
