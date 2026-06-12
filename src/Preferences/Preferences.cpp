#include "Preferences.h"
#include "Utils/XdgDirs.h"
#include <cstdlib>
#include <fstream>
#include <string>
#include <SDL3/SDL_filesystem.h>

// Helper to escape a string for TOML format
static std::string escape_for_toml(const std::string &s) {
    std::string escaped;
    escaped.reserve(s.length());
    for (char c : s) {
        if (c == '"') {
            escaped += "\\\"";
        } else if (c == '\\') {
            escaped += "\\\\";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

// Helper to unescape a TOML quoted string
static std::string unescape_for_toml(const std::string &s) {
    std::string out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '\\' && i + 1 < s.length()) {
            char next_char = s[i + 1];
            if (next_char == '"' || next_char == '\\') {
                out += next_char;
                i++; // skip next char
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string PreferencesManager::GetConfigFilePath() {
    // Configuration lives in $XDG_CONFIG_HOME/InputBridge/ so it follows the
    // XDG Base Directory Specification and lands in ~/.config/InputBridge/ by
    // default rather than the data directory.  AppImage Portable Mode remaps
    // $XDG_CONFIG_HOME automatically when the user places a .config directory
    // next to the .AppImage file, so no special-casing is needed here.
    return XdgDirs::configDir() + CONFIG_FILENAME;
}

void PreferencesManager::Load() {
    m_Sections.clear();
    std::ifstream file(GetConfigFilePath());
    if (file.is_open()) {
        std::string line;
        std::string currentSection; // Top-level keys go into "General"
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            // Trim leading whitespace to check for comments/empty lines
            size_t start = line.find_first_not_of(" \t");
            std::string content = (start == std::string::npos) ? "" : line.substr(start);

            if (content.empty() || content.front() == '#')
                continue;

            if (content.front() == '[' && content.back() == ']') {
                currentSection = content.substr(1, content.size() - 2);
                continue;
            }

            size_t eqPos = content.find('=');
            if (eqPos != std::string::npos) {
                std::string key = content.substr(0, eqPos);
                std::string val = content.substr(eqPos + 1);

                // Trim key
                size_t first = key.find_first_not_of(" \t");
                if (std::string::npos != first) {
                    size_t last = key.find_last_not_of(" \t");
                    key = key.substr(first, (last - first + 1));
                } else {
                    key = "";
                }

                // Trim value (include \r so Windows CRLF files are handled correctly)
                first = val.find_first_not_of(" \t");
                if (std::string::npos != first) {
                    size_t last = val.find_last_not_of(" \t\r");
                    val = val.substr(first, (last - first + 1));
                } else {
                    val = "";
                }

                // Handle quoted strings
                if (val.length() >= 2 && val.front() == '"' && val.back() == '"') {
                    val = val.substr(1, val.length() - 2);
                    // Basic un-escaping for \" and \\
                    val = unescape_for_toml(val);
                }

                if (!key.empty()) {
                    m_Sections[currentSection.empty() ? kGeneralSection : currentSection][key] = val;
                }
            }
        }
        file.close();
    }
}
void PreferencesManager::Save() {
    std::ofstream file(GetConfigFilePath());
    if (file.is_open()) {
        // Save General section first as top-level keys
        if (m_Sections.count(kGeneralSection)) {
            for (const auto &pair : m_Sections.at(kGeneralSection)) {
                file << pair.first << " = \"" << escape_for_toml(pair.second) << "\"\n";
            }
            file << "\n";
        }

        for (const auto &sectionPair : m_Sections) {
            if (sectionPair.first == kGeneralSection)
                continue;
            file << "[" << sectionPair.first << "]\n";
            for (const auto &pair : sectionPair.second) {
                file << pair.first << " = \"" << escape_for_toml(pair.second) << "\"\n";
            }
            file << "\n";
        }
        file.close();
    }
}

void PreferencesManager::SetString(const std::string &key, const std::string &value) { SetString(kGeneralSection, key, value); }

void PreferencesManager::SetString(const std::string &section, const std::string &key, const std::string &value) { m_Sections[section][key] = value; }

std::string PreferencesManager::GetString(const std::string &key, const std::string &defaultValue) const { return GetString(kGeneralSection, key, defaultValue); }

std::string PreferencesManager::GetString(const std::string &section, const std::string &key, const std::string &defaultValue) const {
    if (m_Sections.count(section)) {
        const auto &general = m_Sections.at(section);
        auto it = general.find(key);
        if (it != general.end())
            return it->second;
    }
    return defaultValue;
}

void PreferencesManager::DeleteKey(const std::string &key) { DeleteKey(kGeneralSection, key); }

void PreferencesManager::DeleteKey(const std::string &section, const std::string &key) {
    if (m_Sections.count(section)) {
        m_Sections[section].erase(key);
    }
}

void PreferencesManager::SetInt(const std::string &key, int value) { SetInt(kGeneralSection, key, value); }

void PreferencesManager::SetInt(const std::string &section, const std::string &key, int value) { m_Sections[section][key] = std::to_string(value); }

int PreferencesManager::GetInt(const std::string &key, int defaultValue) const { return GetInt(kGeneralSection, key, defaultValue); }

int PreferencesManager::GetInt(const std::string &section, const std::string &key, int defaultValue) const {
    if (m_Sections.count(section)) {
        const auto &general = m_Sections.at(section);
        auto it = general.find(key);
        if (it != general.end())
            return std::atoi(it->second.c_str());
    }
    return defaultValue;
}

void PreferencesManager::SetFloat(const std::string &key, float value) { SetFloat(kGeneralSection, key, value); }

void PreferencesManager::SetFloat(const std::string &section, const std::string &key, float value) { m_Sections[section][key] = std::to_string(value); }

float PreferencesManager::GetFloat(const std::string &key, float defaultValue) const { return GetFloat(kGeneralSection, key, defaultValue); }

float PreferencesManager::GetFloat(const std::string &section, const std::string &key, float defaultValue) const {
    if (m_Sections.count(section)) {
        const auto &general = m_Sections.at(section);
        auto it = general.find(key);
        if (it != general.end())
            return (float)std::atof(it->second.c_str());
    }
    return defaultValue;
}

void PreferencesManager::SetBool(const std::string &key, bool value) { SetBool(kGeneralSection, key, value); }

void PreferencesManager::SetBool(const std::string &section, const std::string &key, bool value) { m_Sections[section][key] = value ? "true" : "false"; }

bool PreferencesManager::GetBool(const std::string &key, bool defaultValue) const { return GetBool(kGeneralSection, key, defaultValue); }

bool PreferencesManager::GetBool(const std::string &section, const std::string &key, bool defaultValue) const {
    if (m_Sections.count(section)) {
        const auto &general = m_Sections.at(section);
        auto it = general.find(key);
        if (it != general.end()) {
            if (it->second == "true" || it->second == "1") return true;
            if (it->second == "false" || it->second == "0") return false;
        }
    }
    return defaultValue;
}

std::string PreferencesManager::GetVisualizerPreference(const std::string &guid) const {
    std::string section = std::string(kDeviceSectionPrefix) + guid;
    if (m_Sections.count(section)) {
        const auto &sec = m_Sections.at(section);
        auto it = sec.find(kVisualizerKey);
        if (it != sec.end()) {
            return it->second;
        }
    }
    return "";
}

void PreferencesManager::SetVisualizerPreference(const std::string &guid, const std::string &visualizer) { m_Sections[std::string(kDeviceSectionPrefix) + guid][kVisualizerKey] = visualizer; }

std::string PreferencesManager::GetDeviceMapping(const std::string &guid) const {
    std::string section = std::string(kDeviceSectionPrefix) + guid;
    if (m_Sections.count(section)) {
        const auto &sec = m_Sections.at(section);

        // Check for legacy single-line mapping
        auto it = sec.find(kMappingKey);
        if (it != sec.end()) {
            return it->second;
        }

        // Construct from individual keys
        std::string mapping;
        for (const auto &pair : sec) {
            if (pair.first.compare(0, kMappingPrefixLen, kMappingPrefix) == 0) {
                mapping += pair.first.substr(kMappingPrefixLen) + ":" + pair.second + ";";
            }
        }
        return mapping;
    }
    return "";
}

void PreferencesManager::SetDeviceMapping(const std::string &guid, const std::string &mapping) {
    std::string section = std::string(kDeviceSectionPrefix) + guid;

    // Clear any previously decomposed Mapping.* keys
    auto &sec = m_Sections[section];
    for (auto it = sec.begin(); it != sec.end(); ) {
        if (it->first.compare(0, kMappingPrefixLen, kMappingPrefix) == 0)
            it = sec.erase(it);
        else
            ++it;
    }

    // Store as a single opaque value (GetDeviceMapping checks "Mapping" first)
    sec[kMappingKey] = mapping;
    Save();
}

bool PreferencesManager::GetDeviceKeepalive(const std::string &guid) const {
    return GetBool(std::string(kDeviceSectionPrefix) + guid, kKeepaliveKey, false);
}

void PreferencesManager::SetDeviceKeepalive(const std::string &guid, bool enabled) {
    SetBool(std::string(kDeviceSectionPrefix) + guid, kKeepaliveKey, enabled);
    Save();
}

bool PreferencesManager::IsPreferenceApplied(SDL_JoystickID instance_id) const { return m_AppliedPreferences.find(instance_id) != m_AppliedPreferences.end(); }

void PreferencesManager::MarkPreferenceApplied(SDL_JoystickID instance_id) { m_AppliedPreferences.insert(instance_id); }

void PreferencesManager::ClearAppliedPreference(SDL_JoystickID instance_id) { m_AppliedPreferences.erase(instance_id); }