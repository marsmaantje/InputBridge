#pragma once
#include "Protocols/IProtocol.h"
#include <map>

class MarsmaantjeNewProtocol : public IProtocol {
public:
    std::string getProtocolName() const override { return "Marsmaantje (New)"; }

    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address, const std::string &value) override;
    std::string format_wheel(const std::map<std::string, float>& values) override;
    void parse(const std::string& message) override;
};
