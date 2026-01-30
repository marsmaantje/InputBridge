#pragma once
#include <string>
#include <vector>

class IProtocol {
  public:
    virtual ~IProtocol() = default;

    virtual std::string getProtocolName() const = 0;

    // Generic message formatters
    virtual std::string format(const std::string &address, float value) = 0;
    virtual std::string format(const std::string &address, int value) = 0;
    virtual std::string format(const std::string &address,
                               const std::string &value) = 0;

    // Specific message formatters
    virtual std::string format_wheel(float wheel, float brake,
                                     float throttle) = 0;

    virtual void parse(const std::string& message) = 0;
};
