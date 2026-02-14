#pragma once
#include "Protocols/IProtocol.h"
#include <lo/lo.h>

class OSCBaseProtocol : public IProtocol {
public:
    virtual void handle_osc_message(const char* path, const char* types, lo_arg** argv, int argc);

    // IProtocol stubs
    std::string format(const std::string &address, float value) override { return ""; }
    std::string format(const std::string &address, int value) override { return ""; }
    std::string format(const std::string &address, const std::string &value) override { return ""; }
    std::string format_wheel(float wheel, float brake, float throttle, float pitch, float roll) override { return ""; }
    void parse(const std::string& message) override {}
};
