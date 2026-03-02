#pragma once

#include "../Protocols/ProtocolDefinition.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Validation result containing errors and warnings.
 */
struct ValidationResult {
    bool isValid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    /**
     * Add an error message.
     */
    void AddError(const std::string& error) {
        errors.push_back(error);
        isValid = false;
    }
    
    /**
     * Add a warning message.
     */
    void AddWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
    
    /**
     * Check if validation passed (no errors).
     */
    bool IsValid() const {
        return isValid && errors.empty();
    }
    
    /**
     * Get formatted message with all errors and warnings.
     */
    std::string GetFormattedMessage() const {
        std::string message;
        
        if (!errors.empty()) {
            message += "Errors:\n";
            for (const auto& error : errors) {
                message += "  • " + error + "\n";
            }
        }
        
        if (!warnings.empty()) {
            if (!message.empty()) {
                message += "\n";
            }
            message += "Warnings:\n";
            for (const auto& warning : warnings) {
                message += "  • " + warning + "\n";
            }
        }
        
        return message;
    }
};

/**
 * Validates protocol definitions before import.
 * 
 * Performs comprehensive validation including:
 * - JSON structure validation
 * - Field validation
 * - Data type validation
 * - Range checking
 * - Consistency checks
 */
class ProtocolValidator {
public:
    /**
     * Validate a protocol JSON file.
     * 
     * @param filePath Path to protocol JSON file
     * @return Validation result with errors and warnings
     */
    static ValidationResult ValidateProtocolFile(const std::string& filePath);
    
    /**
     * Validate a protocol definition object.
     * 
     * @param definition Protocol definition to validate
     * @return Validation result with errors and warnings
     */
    static ValidationResult ValidateProtocolDefinition(const ProtocolDefinition& definition);
    
    /**
     * Validate JSON structure for protocol.
     * 
     * @param j JSON object to validate
     * @return Validation result with errors and warnings
     */
    static ValidationResult ValidateProtocolJSON(const json& j);
    
    /**
     * Validate protocol name.
     * 
     * @param name Protocol name to validate
     * @return Empty string if valid, error message otherwise
     */
    static std::string ValidateProtocolName(const std::string& name);
    
    /**
     * Validate OSC host address.
     * 
     * @param host Host address to validate
     * @return Empty string if valid, error message otherwise
     */
    static std::string ValidateHostAddress(const std::string& host);
    
    /**
     * Validate port number.
     * 
     * @param port Port number to validate
     * @param portType Type of port (for error message)
     * @return Empty string if valid, error message otherwise
     */
    static std::string ValidatePort(int port, const std::string& portType = "Port");
    
    /**
     * Validate OSC path format.
     * 
     * @param path OSC path to validate
     * @return Empty string if valid, error message otherwise
     */
    static std::string ValidateOSCPath(const std::string& path);
    
    /**
     * Validate WebSocket key format.
     * 
     * @param key WebSocket key to validate
     * @return Empty string if valid, error message otherwise
     */
    static std::string ValidateWSKey(const std::string& key);
    
    /**
     * Check if a field ID is valid.
     * 
     * @param fieldId Field ID to validate
     * @return Empty string if valid, error message otherwise
     */
    static std::string ValidateFieldId(const std::string& fieldId);

private:
    static bool IsValidIPAddress(const std::string& ip);
    static bool IsValidHostname(const std::string& hostname);
};
