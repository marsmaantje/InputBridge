#include "ProtocolValidator.h"
#include <fstream>
#include <regex>
#include <cctype>

namespace {
    const std::string KEY_ID = "id";
    const std::string KEY_NAME = "name";
    const std::string KEY_TRANSPORT = "transport";
    const std::string KEY_DIRECTION = "direction";
    const std::string KEY_OSC_HOST = "oscHost";
    const std::string KEY_OSC_SEND_PORT = "oscSendPort";
    const std::string KEY_OSC_RECV_PORT = "oscRecvPort";
    const std::string KEY_WSS_PORT = "wssPort";
    const std::string KEY_FIELDS = "fields";
    const std::string KEY_FIELD_ID = "fieldId";
    const std::string KEY_ENABLED = "enabled";
    const std::string KEY_OSC_PATH = "oscPath";
    const std::string KEY_WS_KEY = "wsKey";

    const std::string VAL_TRANSPORT_OSC = "osc";
    const std::string VAL_TRANSPORT_WEBSOCKET = "websocket";
    const std::string VAL_DIRECTION_OUTPUT = "output";
    const std::string VAL_DIRECTION_INPUT = "input";

    // Port types for validation messages
    const std::string PORT_TYPE_OSC_SEND = "OSC send port";
    const std::string PORT_TYPE_OSC_RECV = "OSC receive port";
    const std::string PORT_TYPE_WSS = "WebSocket port";

    // Hostnames
    const std::string VAL_LOCALHOST = "localhost";

    // Validation messages
    const std::string ERR_CANNOT_OPEN_FILE = "Cannot open file: ";
    const std::string ERR_INVALID_JSON = "Invalid JSON format: ";
    const std::string ERR_PROTOCOL_ID_EMPTY = "Protocol ID cannot be empty";
    const std::string ERR_PROTOCOL_NAME_EMPTY = "Protocol name cannot be empty";
    const std::string ERR_PROTOCOL_NAME_TOO_LONG = "Protocol name too long (maximum 127 characters)";
    const std::string ERR_PROTOCOL_NAME_INVALID_CHARS = "Protocol name contains invalid control characters";
    const std::string ERR_HOST_EMPTY = "Host address cannot be empty";
    const std::string ERR_HOST_INVALID_FORMAT = "Invalid host address format";
    const std::string ERR_PORT_RANGE = " must be between 1 and 65535";
    const std::string ERR_OSC_PATH_EMPTY = "OSC path cannot be empty";
    const std::string ERR_OSC_PATH_NO_SLASH = "OSC path must start with '/'";
    const std::string ERR_OSC_PATH_INVALID_CHAR = "OSC path contains invalid character: '";
    const std::string ERR_OSC_PATH_DOUBLE_SLASH = "OSC path cannot contain '//'";
    const std::string ERR_WS_KEY_EMPTY = "WebSocket key cannot be empty";
    const std::string ERR_WS_KEY_TOO_LONG = "WebSocket key too long (maximum 64 characters)";
    const std::string ERR_WS_KEY_INVALID_CHARS = "WebSocket key can only contain letters, numbers, '_', and '-'";
    const std::string ERR_WS_KEY_STARTS_WITH_NUM = "WebSocket key should not start with a number";
    const std::string ERR_FIELD_ID_EMPTY = "Field ID cannot be empty";
    const std::string ERR_FIELD_ID_TOO_LONG = "Field ID too long (maximum 64 characters)";
    const std::string ERR_FIELD_ID_INVALID_CHARS = "Field ID can only contain letters, numbers, and '_'";

    const std::string WARN_NO_FIELDS = "Protocol has no fields defined";
    const std::string WARN_MISSING_OSC_HOST = "Missing OSC host, will use default";
    const std::string WARN_MISSING_OSC_PORTS = "Missing OSC ports, will use defaults";
    const std::string WARN_MISSING_WSS_PORT = "Missing WebSocket port, will use default";
}

ValidationResult ProtocolValidator::ValidateProtocolFile(const std::string& filePath) {
    ValidationResult result;

    // Check file exists
    std::ifstream file(filePath);
    if (!file.is_open()) {
        result.AddError(ERR_CANNOT_OPEN_FILE + filePath);
        return result;
    }

    // Parse JSON
    json j;
    try {
        file >> j;
    } catch (const json::exception& e) {
        result.AddError(ERR_INVALID_JSON + std::string(e.what()));
        return result;
    }

    // Validate JSON structure
    return ValidateProtocolJSON(j);
}

ValidationResult ProtocolValidator::ValidateProtocolDefinition(const ProtocolDefinition& definition) {
    ValidationResult result;

    // Validate name
    std::string nameError = ValidateProtocolName(definition.name);
    if (!nameError.empty()) {
        result.AddError(nameError);
    }

    // Validate ID
    if (definition.id.empty()) {
        result.AddError(ERR_PROTOCOL_ID_EMPTY);
    }

    // Validate transport-specific settings
    if (definition.transport == ProtocolTransport::OSC) {
        std::string hostError = ValidateHostAddress(definition.oscHost);
        if (!hostError.empty()) {
            result.AddError(hostError);
        }

        if (definition.direction == ProtocolDirection::Output) {
            std::string portError = ValidatePort(definition.oscSendPort, PORT_TYPE_OSC_SEND);
            if (!portError.empty()) {
                result.AddError(portError);
            }
        } else {
            std::string portError = ValidatePort(definition.oscRecvPort, PORT_TYPE_OSC_RECV);
            if (!portError.empty()) {
                result.AddError(portError);
            }
        }
    } else { // WebSocket
        std::string portError = ValidatePort(definition.wssPort, PORT_TYPE_WSS);
        if (!portError.empty()) {
            result.AddError(portError);
        }
    }

    // Validate fields
    if (definition.fields.empty()) {
        result.AddWarning(WARN_NO_FIELDS);
    }

    for (const auto& field : definition.fields) {
        std::string fieldError = ValidateFieldId(field.fieldId);
        if (!fieldError.empty()) {
            result.AddError("Field '" + field.fieldId + "': " + fieldError);
        }

        if (definition.transport == ProtocolTransport::OSC) {
            std::string oscError = ValidateOSCPath(field.oscPath);
            if (!oscError.empty()) {
                result.AddError("Field '" + field.fieldId + "' OSC path: " + oscError);
            }
        } else {
            std::string wsError = ValidateWSKey(field.wsKey);
            if (!wsError.empty()) {
                result.AddError("Field '" + field.fieldId + "' WS key: " + wsError);
            }
        }
    }

    return result;
}

ValidationResult ProtocolValidator::ValidateProtocolJSON(const json& j) {
    ValidationResult result;

    // Check required fields
    if (!j.contains(KEY_ID)) {
        result.AddError("Missing required field: '" + KEY_ID + "'");
    }
    if (!j.contains(KEY_NAME)) {
        result.AddError("Missing required field: '" + KEY_NAME + "'");
    }
    if (!j.contains(KEY_TRANSPORT)) {
        result.AddError("Missing required field: '" + KEY_TRANSPORT + "'");
    }
    if (!j.contains(KEY_DIRECTION)) {
        result.AddError("Missing required field: '" + KEY_DIRECTION + "'");
    }

    // Validate transport
    if (j.contains(KEY_TRANSPORT)) {
        std::string transport = j[KEY_TRANSPORT];
        if (transport != VAL_TRANSPORT_OSC && transport != VAL_TRANSPORT_WEBSOCKET) {
            result.AddError("Invalid transport: must be '" + VAL_TRANSPORT_OSC + "' or '" + VAL_TRANSPORT_WEBSOCKET + "'");
        }

        // Validate transport-specific fields
        if (transport == VAL_TRANSPORT_OSC) {
            if (!j.contains(KEY_OSC_HOST)) {
                result.AddWarning(WARN_MISSING_OSC_HOST);
            }
            if (!j.contains(KEY_OSC_SEND_PORT) && !j.contains(KEY_OSC_RECV_PORT)) {
                result.AddWarning(WARN_MISSING_OSC_PORTS);
            }
        } else if (transport == VAL_TRANSPORT_WEBSOCKET) {
            if (!j.contains(KEY_WSS_PORT)) {
                result.AddWarning(WARN_MISSING_WSS_PORT);
            }
        }
    }

    // Validate direction
    if (j.contains(KEY_DIRECTION)) {
        std::string direction = j[KEY_DIRECTION];
        if (direction != VAL_DIRECTION_OUTPUT && direction != VAL_DIRECTION_INPUT) {
            result.AddError("Invalid direction: must be '" + VAL_DIRECTION_OUTPUT + "' or '" + VAL_DIRECTION_INPUT + "'");
        }
    }

    // Validate fields array
    if (j.contains(KEY_FIELDS)) {
        if (!j[KEY_FIELDS].is_array()) {
            result.AddError("'" + KEY_FIELDS + "' must be an array");
        } else {
            const auto& fields = j[KEY_FIELDS];
            if (fields.empty()) {
                result.AddWarning(WARN_NO_FIELDS);
            }

            for (size_t i = 0; i < fields.size(); ++i) {
                const auto& field = fields[i];

                if (!field.contains(KEY_FIELD_ID)) {
                    result.AddError("Field " + std::to_string(i) + " missing '" + KEY_FIELD_ID + "'");
                }
                if (!field.contains(KEY_ENABLED)) {
                    result.AddWarning("Field " + std::to_string(i) + " missing '" + KEY_ENABLED + "' flag");
                }

                if (j[KEY_TRANSPORT] == VAL_TRANSPORT_OSC && !field.contains(KEY_OSC_PATH)) {
                    result.AddWarning("Field " + std::to_string(i) + " missing '" + KEY_OSC_PATH + "'");
                }
                if (j[KEY_TRANSPORT] == VAL_TRANSPORT_WEBSOCKET && !field.contains(KEY_WS_KEY)) {
                    result.AddWarning("Field " + std::to_string(i) + " missing '" + KEY_WS_KEY + "'");
                }
            }
        }
    } else {
        result.AddWarning("Protocol has no '" + KEY_FIELDS + "' array");
    }

    return result;
}

std::string ProtocolValidator::ValidateProtocolName(const std::string& name) {
    if (name.empty()) {
        return ERR_PROTOCOL_NAME_EMPTY;
    }

    if (name.length() > 127) {
        return ERR_PROTOCOL_NAME_TOO_LONG;
    }

    // Check for invalid characters (very permissive, just no control characters)
    for (char c : name) {
        if (std::iscntrl(static_cast<unsigned char>(c))) {
            return ERR_PROTOCOL_NAME_INVALID_CHARS;
        }
    }

    return "";
}

std::string ProtocolValidator::ValidateHostAddress(const std::string& host) {
    if (host.empty()) {
        return ERR_HOST_EMPTY;
    }

    // Check if it's a valid IP address or hostname
    if (!IsValidIPAddress(host) && !IsValidHostname(host)) {
        return ERR_HOST_INVALID_FORMAT;
    }

    return "";
}

std::string ProtocolValidator::ValidatePort(int port, const std::string& portType) {
    if (port < 1 || port > 65535) {
        return portType + ERR_PORT_RANGE;
    }

    // Warn about privileged ports
    if (port < 1024) {
        return ""; // Don't error, but caller can check
    }

    return "";
}

std::string ProtocolValidator::ValidateOSCPath(const std::string& path) {
    if (path.empty()) {
        return ERR_OSC_PATH_EMPTY;
    }

    if (path[0] != '/') {
        return ERR_OSC_PATH_NO_SLASH;
    }

    // Check for invalid characters in OSC path
    // OSC paths can contain alphanumeric, /, _, -, .
    for (char c : path) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '/' && c != '_' && c != '-' && c != '.') {
            return ERR_OSC_PATH_INVALID_CHAR + std::string(1, c) + "'";
        }
    }

    // Check for double slashes
    if (path.find("//") != std::string::npos) {
        return ERR_OSC_PATH_DOUBLE_SLASH;
    }

    return "";
}

std::string ProtocolValidator::ValidateWSKey(const std::string& key) {
    if (key.empty()) {
        return ERR_WS_KEY_EMPTY;
    }

    if (key.length() > 64) {
        return ERR_WS_KEY_TOO_LONG;
    }

    // Check for valid characters (alphanumeric, underscore, hyphen)
    for (char c : key) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return ERR_WS_KEY_INVALID_CHARS;
        }
    }

    // Should not start with a number (for better compatibility)
    if (std::isdigit(static_cast<unsigned char>(key[0]))) {
        return ERR_WS_KEY_STARTS_WITH_NUM;
    }

    return "";
}

std::string ProtocolValidator::ValidateFieldId(const std::string& fieldId) {
    if (fieldId.empty()) {
        return ERR_FIELD_ID_EMPTY;
    }

    if (fieldId.length() > 64) {
        return ERR_FIELD_ID_TOO_LONG;
    }

    // Check for valid characters (alphanumeric, underscore)
    for (char c : fieldId) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return ERR_FIELD_ID_INVALID_CHARS;
        }
    }

    return "";
}

bool ProtocolValidator::IsValidIPAddress(const std::string& ip) {
    // Simple IPv4 validation
    std::regex ipv4Pattern(
        R"(^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)"
    );

    // Simple IPv6 validation (basic check)
    std::regex ipv6Pattern(R"(^([0-9a-fA-F]{0,4}:){7}[0-9a-fA-F]{0,4}$)");

    return std::regex_match(ip, ipv4Pattern) || std::regex_match(ip, ipv6Pattern);
}

bool ProtocolValidator::IsValidHostname(const std::string& hostname) {
    if (hostname.empty() || hostname.length() > 253) {
        return false;
    }

    // Allow localhost
    if (hostname == VAL_LOCALHOST) {
        return true;
    }

    // Basic hostname validation
    // Each label must be 1-63 characters, contain only alphanumeric and hyphens,
    // not start or end with hyphen
    std::regex hostnamePattern(
        R"(^([a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?\.)*[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?$)"
    );

    return std::regex_match(hostname, hostnamePattern);
}
