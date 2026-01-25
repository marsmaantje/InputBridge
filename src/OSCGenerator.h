#pragma once
#include <string>

class OSCGenerator {
public:
    static std::string Message(const std::string& address, float value);
    static std::string Message(const std::string& address, int value);
};
