#pragma once

class NetworkStatusWindow {
public:
    static void Draw(int& update_rate, bool& dynamic_rate, float messages_per_second);
};