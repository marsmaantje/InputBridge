#pragma once

#include "Protocols/IProtocol.h"
#include "Network/OSCServer.h"
#include <map>

class OSCProtocol : public IProtocol {
public:
    OSCProtocol();
    ~OSCProtocol() override;

    std::string getProtocolName() const override { return "OSC"; }

    // IProtocol implementation (mostly unused as OSCServer handles sending directly)
    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address, const std::string &value) override;
    std::string format_wheel(const std::map<std::string, float>& values) override;
    bool parse(const std::string& message) override;

private:
    bool handle_osc_message(const char* path, const char* types, lo_arg** argv, int argc);
};
