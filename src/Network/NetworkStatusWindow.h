#pragma once

class NetworkStatusWindow {
public:
    /**
     * Draw the Network Server panel.
     */
    static void Draw(int& update_rate, bool& dynamic_rate, float messages_per_second);
};