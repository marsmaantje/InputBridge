#pragma once

class AboutWindow {
public:
    /**
     * Draw only the About page content (no Begin/End wrapper).
     * Call this inside a child window or content area.
     */
    static void DrawContent();
};