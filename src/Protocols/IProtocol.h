#pragma once
#include <string>

/**
 * Base protocol interface.
 *
 * Direction terminology used throughout the protocol subsystem:
 *   Output  – data that flows from the server TO the client (input / sensor data).
 *   Input   – data that flows FROM the client TO the server (haptic commands, etc.).
 *
 * The transport-agnostic ProtocolDefinition (see Protocols/ProtocolDefinition.h)
 * governs which fields are enabled and how they are addressed at runtime.
 * IProtocol implementations handle low-level serialisation only.
 */
class IProtocol {
  public:
    virtual ~IProtocol() = default;

    virtual std::string getProtocolName() const = 0;

    // Generic message formatters
    virtual std::string format(const std::string &address, float value) = 0;
    virtual std::string format(const std::string &address, int value) = 0;
    virtual std::string format(const std::string &address, const std::string &value) = 0;

    // Specific message formatters
    virtual std::string format_wheel(float wheel, float brake, float throttle, float pitch, float roll) = 0;
    virtual void parse(const std::string& message) = 0;
};
