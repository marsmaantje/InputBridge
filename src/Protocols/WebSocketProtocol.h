#pragma once
#include "IProtocol.h"

class WebSocketProtocol : public IProtocol {
  public:
    enum class WheelProtocolVersion { MarsmaantjeOld, MarsmaantjeNew };

    WebSocketProtocol(
        WheelProtocolVersion version = WheelProtocolVersion::MarsmaantjeNew);

    void setWheelProtocolVersion(WheelProtocolVersion version);
    WheelProtocolVersion getWheelProtocolVersion() const;

    std::string getProtocolName() const override;

    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address,
                       const std::string &value) override;

    std::string format_wheel(float wheel, float brake, float throttle) override;

    static const char *GetVersionLabel(int index);
    static int GetVersionCount();

  private:
    WheelProtocolVersion m_version;
};
