#pragma once
#include "IProtocol.h"
#include <nlohmann/json.hpp>

class WebSocketProtocol : public IProtocol {
  public:
    enum class ProtocolVersion { MarsmaantjeOld, MarsmaantjeNew };

    WebSocketProtocol(ProtocolVersion version = ProtocolVersion::MarsmaantjeNew);

    void setProtocolVersion(ProtocolVersion version);
    ProtocolVersion getProtocolVersion() const;

    std::string getProtocolName() const override;

    std::string format(const std::string &address, float value) override;
    std::string format(const std::string &address, int value) override;
    std::string format(const std::string &address, const std::string &value) override;

    std::string format_wheel(float wheel, float brake, float throttle, float pitch, float roll) override;

    void parse(const std::string& message) override;

    static const char *GetVersionLabel(int index);
    static int GetVersionCount();

  private:
    ProtocolVersion m_version;
};
