#include "ProtocolValidator.h"
#include <fstream>
#include <regex>
#include <algorithm>
#include <cctype>

ValidationResult ProtocolValidator::ValidateProtocolFile(const std::string& filePath) {
    ValidationResult result;
    
    // Check file exists
    std::ifstream file(filePath);
    if (!file.is_open()) {
        result.AddError("Cannot open file: " + filePath);
        return result;
    }
    
    // Parse JSON
    json j;
    try {
        file >> j;
    } catch (const json::exception& e) {
        result.AddError("Invalid JSON format: " + std::string(e.what()));
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
        result.AddError("Protocol ID cannot be empty");
    }
    
    // Validate transport-specific settings
    if (definition.transport == ProtocolTransport::OSC) {
        std::string hostError = ValidateHostAddress(definition.oscHost);
        if (!hostError.empty()) {
            result.AddError(hostError);
        }
        
        if (definition.direction == ProtocolDirection::Output) {
            std::string portError = ValidatePort(definition.oscSendPort, "OSC send port");
            if (!portError.empty()) {
                result.AddError(portError);
            }
        } else {
            std::string portError = ValidatePort(definition.oscRecvPort, "OSC receive port");
            if (!portError.empty()) {
                result.AddError(portError);
            }
        }
    } else { // WebSocket
        std::string portError = ValidatePort(definition.wssPort, "WebSocket port");
        if (!portError.empty()) {
            result.AddError(portError);
        }
    }
    
    // Validate fields
    if (definition.fields.empty()) {
        result.AddWarning("Protocol has no fields defined");
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
    if (!j.contains("id")) {
        result.AddError("Missing required field: 'id'");
    }
    if (!j.contains("name")) {
        result.AddError("Missing required field: 'name'");
    }
    if (!j.contains("transport")) {
        result.AddError("Missing required field: 'transport'");
    }
    if (!j.contains("direction")) {
        result.AddError("Missing required field: 'direction'");
    }
    
    // Validate transport
    if (j.contains("transport")) {
        std::string transport = j["transport"];
        if (transport != "osc" && transport != "websocket") {
            result.AddError("Invalid transport: must be 'osc' or 'websocket'");
        }
        
        // Validate transport-specific fields
        if (transport == "osc") {
            if (!j.contains("oscHost")) {
                result.AddWarning("Missing OSC host, will use default");
            }
            if (!j.contains("oscSendPort") && !j.contains("oscRecvPort")) {
                result.AddWarning("Missing OSC ports, will use defaults");
            }
        } else if (transport == "websocket") {
            if (!j.contains("wssPort")) {
                result.AddWarning("Missing WebSocket port, will use default");
            }
        }
    }
    
    // Validate direction
    if (j.contains("direction")) {
        std::string direction = j["direction"];
        if (direction != "output" && direction != "input") {
            result.AddError("Invalid direction: must be 'output' or 'input'");
        }
    }
    
    // Validate fields array
    if (j.contains("fields")) {
        if (!j["fields"].is_array()) {
            result.AddError("'fields' must be an array");
        } else {
            const auto& fields = j["fields"];
            if (fields.empty()) {
                result.AddWarning("Protocol has no fields defined");
            }
            
            for (size_t i = 0; i < fields.size(); ++i) {
                const auto& field = fields[i];
                
                if (!field.contains("fieldId")) {
                    result.AddError("Field " + std::to_string(i) + " missing 'fieldId'");
                }
                if (!field.contains("enabled")) {
                    result.AddWarning("Field " + std::to_string(i) + " missing 'enabled' flag");
                }
                
                if (j["transport"] == "osc" && !field.contains("oscPath")) {
                    result.AddWarning("Field " + std::to_string(i) + " missing 'oscPath'");
                }
                if (j["transport"] == "websocket" && !field.contains("wsKey")) {
                    result.AddWarning("Field " + std::to_string(i) + " missing 'wsKey'");
                }
            }
        }
    } else {
        result.AddWarning("Protocol has no 'fields' array");
    }
    
    return result;
}

std::string ProtocolValidator::ValidateProtocolName(const std::string& name) {
    if (name.empty()) {
        return "Protocol name cannot be empty";
    }
    
    if (name.length() > 127) {
        return "Protocol name too long (maximum 127 characters)";
    }
    
    // Check for invalid characters (very permissive, just no control characters)
    for (char c : name) {
        if (std::iscntrl(static_cast<unsigned char>(c))) {
            return "Protocol name contains invalid control characters";
        }
    }
    
    return "";
}

std::string ProtocolValidator::ValidateHostAddress(const std::string& host) {
    if (host.empty()) {
        return "Host address cannot be empty";
    }
    
    // Check if it's a valid IP address or hostname
    if (!IsValidIPAddress(host) && !IsValidHostname(host)) {
        return "Invalid host address format";
    }
    
    return "";
}

std::string ProtocolValidator::ValidatePort(int port, const std::string& portType) {
    if (port < 1 || port > 65535) {
        return portType + " must be between 1 and 65535";
    }
    
    // Warn about privileged ports
    if (port < 1024) {
        return ""; // Don't error, but caller can check
    }
    
    return "";
}

std::string ProtocolValidator::ValidateOSCPath(const std::string& path) {
    if (path.empty()) {
        return "OSC path cannot be empty";
    }
    
    if (path[0] != '/') {
        return "OSC path must start with '/'";
    }
    
    // Check for invalid characters in OSC path
    // OSC paths can contain alphanumeric, /, _, -, .
    for (char c : path) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && 
            c != '/' && c != '_' && c != '-' && c != '.') {
            return "OSC path contains invalid character: '" + std::string(1, c) + "'";
        }
    }
    
    // Check for double slashes
    if (path.find("//") != std::string::npos) {
        return "OSC path cannot contain '//'";
    }
    
    return "";
}

std::string ProtocolValidator::ValidateWSKey(const std::string& key) {
    if (key.empty()) {
        return "WebSocket key cannot be empty";
    }
    
    if (key.length() > 64) {
        return "WebSocket key too long (maximum 64 characters)";
    }
    
    // Check for valid characters (alphanumeric, underscore, hyphen)
    for (char c : key) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return "WebSocket key can only contain letters, numbers, '_', and '-'";
        }
    }
    
    // Should not start with a number (for better compatibility)
    if (std::isdigit(static_cast<unsigned char>(key[0]))) {
        return "WebSocket key should not start with a number";
    }
    
    return "";
}

std::string ProtocolValidator::ValidateFieldId(const std::string& fieldId) {
    if (fieldId.empty()) {
        return "Field ID cannot be empty";
    }
    
    if (fieldId.length() > 64) {
        return "Field ID too long (maximum 64 characters)";
    }
    
    // Check for valid characters (alphanumeric, underscore)
    for (char c : fieldId) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return "Field ID can only contain letters, numbers, and '_'";
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
    if (hostname == "localhost") {
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
