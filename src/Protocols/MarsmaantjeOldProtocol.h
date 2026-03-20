#pragma once
#include "Protocols/IProtocol.h"
#include <map>

class MarsmaantjeOldProtocol : public IProtocol {
public:
    std::string getProtocolName() const override { return "Marsmaantje (Old)"; }

    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address, const std::string &value) override;
    std::string format_wheel(const std::map<std::string, float>& values) override;
    bool parse(const std::string& message) override;
};
