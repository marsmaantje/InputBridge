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
            escaped += R"(\")";
        } else if (c == '\\') {
            escaped += R"(\\)";
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
        if (s[i] == '\\' && i + 1 < s.length() && (s[i + 1] == '"' || s[i + 1] == '\\')) {
            out += s[i + 1];
            ++i; // skip the escaped character; the loop's ++i skips the backslash
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string PreferencesManager::GetConfigFilePath() const {
    // Configuration lives in $XDG_CONFIG_HOME/InputBridge/ so it follows the
    // XDG Base Directory Specification and lands in ~/.config/InputBridge/ by
    // default rather than the data directory.  AppImage Portable Mode remaps
    // $XDG_CONFIG_HOME automatically when the user places a .config directory
    // next to the .AppImage file, so no special-casing is needed here.
    return XdgDirs::configDir() + CONFIG_FILENAME;
}

static std::string trim_config_value(const std::string &value, const std::string &whitespace) {
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos)
        return "";
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

static bool parse_config_line(const std::string &line, std::string &currentSection,
                              std::string &key, std::string &value) {
    const size_t start = line.find_first_not_of(" \t");
    const std::string content = start == std::string::npos ? "" : line.substr(start);
    if (content.empty() || content.front() == '#')
        return false;

    if (content.front() == '[' && content.back() == ']') {
        currentSection = content.substr(1, content.size() - 2);
        return false;
    }

    const size_t eqPos = content.find('=');
    if (eqPos == std::string::npos)
        return false;

    key = trim_config_value(content.substr(0, eqPos), " \t");
    value = trim_config_value(content.substr(eqPos + 1), " \t\r");
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = unescape_for_toml(value.substr(1, value.size() - 2));
    }
    return !key.empty();
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
            std::string key;
            std::string value;
            if (parse_config_line(line, currentSection, key, value)) {
                m_Sections[currentSection.empty() ? kGeneralSection : currentSection][key] = value;
            }
        }
        file.close();
    }
}
void PreferencesManager::Save() {
    std::ofstream file(GetConfigFilePath());
    if (file.is_open()) {
        // Save General section first as top-level keys
        if (m_Sections.contains(kGeneralSection)) {
            for (const auto &[key, value] : m_Sections.at(kGeneralSection)) {
                file << key << " = \"" << escape_for_toml(value) << "\"\n";
            }
            file << "\n";
        }

        for (const auto &[sectionName, sectionEntries] : m_Sections) {
            if (sectionName == kGeneralSection)
                continue;
            file << "[" << sectionName << "]\n";
            for (const auto &[key, value] : sectionEntries) {
                file << key << " = \"" << escape_for_toml(value) << "\"\n";
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
    if (m_Sections.contains(section)) {
        const auto &general = m_Sections.at(section);
        auto it = general.find(key);
        if (it != general.end())
            return it->second;
    }
    return defaultValue;
}

void PreferencesManager::DeleteKey(const std::string &key) { DeleteKey(kGeneralSection, key); }

void PreferencesManager::DeleteKey(const std::string &section, const std::string &key) {
    if (m_Sections.contains(section)) {
        m_Sections[section].erase(key);
    }
}

void PreferencesManager::SetInt(const std::string &key, int value) { SetInt(kGeneralSection, key, value); }

void PreferencesManager::SetInt(const std::string &section, const std::string &key, int value) { m_Sections[section][key] = std::to_string(value); }

int PreferencesManager::GetInt(const std::string &key, int defaultValue) const { return GetInt(kGeneralSection, key, defaultValue); }

int PreferencesManager::GetInt(const std::string &section, const std::string &key, int defaultValue) const {
    if (m_Sections.contains(section)) {
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
    if (m_Sections.contains(section)) {
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
    if (m_Sections.contains(section)) {
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

int PreferencesManager::GetWiimotePlayerLED(const std::string &hid_path, int defaultValue) const {
    return GetInt(std::string(kWiimoteSectionPrefix) + hid_path, kWiiPlayerLEDKey, defaultValue);
}

void PreferencesManager::SetWiimotePlayerLED(const std::string &hid_path, int player_1to4) {
    SetInt(std::string(kWiimoteSectionPrefix) + hid_path, kWiiPlayerLEDKey, player_1to4);
    Save();
}

bool PreferencesManager::GetWiimoteIRExtendedMode(const std::string &hid_path, bool defaultValue) const {
    return GetBool(std::string(kWiimoteSectionPrefix) + hid_path, kWiiIRExtendedKey, defaultValue);
}

void PreferencesManager::SetWiimoteIRExtendedMode(const std::string &hid_path, bool enabled) {
    SetBool(std::string(kWiimoteSectionPrefix) + hid_path, kWiiIRExtendedKey, enabled);
    Save();
}

int PreferencesManager::GetWiimoteIRMode(const std::string &hid_path, int defaultValue) const {
    return GetInt(std::string(kWiimoteSectionPrefix) + hid_path, kWiiIRModeKey, defaultValue);
}

void PreferencesManager::SetWiimoteIRMode(const std::string &hid_path, int mode) {
    SetInt(std::string(kWiimoteSectionPrefix) + hid_path, kWiiIRModeKey, mode);
    Save();
}

bool PreferencesManager::GetWiimoteBalanceTareKg(const std::string &hid_path, float outKg[4]) const {
    std::string section = std::string(kWiimoteSectionPrefix) + hid_path;
    if (!m_Sections.contains(section))
        return false;

    bool any = false;
    for (int i = 0; i < 4; ++i) {
        std::string key = std::string(kWiiTareKeyPrefix) + std::to_string(i);
        const auto &sec = m_Sections.at(section);
        auto it = sec.find(key);
        if (it == sec.end())
            return false; // partial/missing entry - treat as "nothing saved"
        outKg[i] = (float)std::atof(it->second.c_str());
        if (outKg[i] != 0.f)
            any = true;
    }
    return any;
}

void PreferencesManager::SetWiimoteBalanceTareKg(const std::string &hid_path, const float kg[4]) {
    std::string section = std::string(kWiimoteSectionPrefix) + hid_path;
    for (int i = 0; i < 4; ++i) {
        std::string key = std::string(kWiiTareKeyPrefix) + std::to_string(i);
        m_Sections[section][key] = std::to_string(kg[i]);
    }
    Save();
}

void PreferencesManager::ClearWiimoteBalanceTareKg(const std::string &hid_path) {
    std::string section = std::string(kWiimoteSectionPrefix) + hid_path;
    if (!m_Sections.contains(section))
        return;
    for (int i = 0; i < 4; ++i)
        m_Sections[section].erase(std::string(kWiiTareKeyPrefix) + std::to_string(i));
    Save();
}

bool PreferencesManager::IsPreferenceApplied(SDL_JoystickID instance_id) const { 
    return m_AppliedPreferences.contains(instance_id); 
}

void PreferencesManager::MarkPreferenceApplied(SDL_JoystickID instance_id) { 
    m_AppliedPreferences.insert(instance_id); 
}

void PreferencesManager::ClearAppliedPreference(SDL_JoystickID instance_id) { 
    m_AppliedPreferences.erase(instance_id); 
}
