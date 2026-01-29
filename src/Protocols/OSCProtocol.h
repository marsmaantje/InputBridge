#pragma once
#include "IProtocol.h"

class OSCProtocol : public IProtocol {
  public:
    std::string getProtocolName() const override;

    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address,
                       const std::string &value) override;

    // This is not applicable to OSC, so it will return an empty string.
    std::string format_wheel(float wheel, float brake, float throttle) override;
};
