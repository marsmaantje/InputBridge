#pragma once

#include <string_view>

class OutputMapper;

class HapticParser {
public:
    // Parses a JSON message for generic haptic commands and queues them.
    static void Parse(std::string_view message, OutputMapper* mapper);
};