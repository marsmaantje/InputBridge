#include "Preferences.h"
#include <cstdlib>
#include <fstream>

void PreferencesManager::Load() {
    m_Sections.clear();
    std::ifstream file(CONFIG_FILENAME);
    if (file.is_open()) {
        std::string line;
        std::string currentSection = "General";
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = line.substr(0, eqPos);
                std::string val = line.substr(eqPos + 1);
                if (!key.empty()) {
                    m_Sections[currentSection][key] = val;
                }
            }
        }
        file.close();
    }
}

void PreferencesManager::Save() {
    std::ofstream file(CONFIG_FILENAME);
    if (file.is_open()) {
        // Save General section first
        if (m_Sections.count("General")) {
            file << "[General]\n";
            for (const auto &pair : m_Sections.at("General")) {
                file << pair.first << "=" << pair.second << "\n";
            }
            file << "\n";
        }

        for (const auto &sectionPair : m_Sections) {
            if (sectionPair.first == "General")
                continue;
            file << "[" << sectionPair.first << "]\n";
            for (const auto &pair : sectionPair.second) {
                file << pair.first << "=" << pair.second << "\n";
            }
            file << "\n";
        }
        file.close();
    }
}

void PreferencesManager::SetString(const std::string &key,
                                   const std::string &value) {
    m_Sections["General"][key] = value;
}

std::string
PreferencesManager::GetString(const std::string &key,
                              const std::string &defaultValue) const {
    if (m_Sections.count("General")) {
        const auto &general = m_Sections.at("General");
        auto it = general.find(key);
        if (it != general.end())
            return it->second;
    }
    return defaultValue;
}

void PreferencesManager::DeleteKey(const std::string &key) {
    if (m_Sections.count("General")) {
        m_Sections["General"].erase(key);
    }
}

void PreferencesManager::SetInt(const std::string &key, int value) {
    m_Sections["General"][key] = std::to_string(value);
}

int PreferencesManager::GetInt(const std::string &key, int defaultValue) const {
    if (m_Sections.count("General")) {
        const auto &general = m_Sections.at("General");
        auto it = general.find(key);
        if (it != general.end())
            return std::atoi(it->second.c_str());
    }
    return defaultValue;
}

void PreferencesManager::SetFloat(const std::string &key, float value) {
    m_Sections["General"][key] = std::to_string(value);
}

float PreferencesManager::GetFloat(const std::string &key,
                                   float defaultValue) const {
    if (m_Sections.count("General")) {
        const auto &general = m_Sections.at("General");
        auto it = general.find(key);
        if (it != general.end())
            return (float)std::atof(it->second.c_str());
    }
    return defaultValue;
}

void PreferencesManager::SetBool(const std::string &key, bool value) {
    m_Sections["General"][key] = value ? "1" : "0";
}

bool PreferencesManager::GetBool(const std::string &key,
                                 bool defaultValue) const {
    if (m_Sections.count("General")) {
        const auto &general = m_Sections.at("General");
        auto it = general.find(key);
        if (it != general.end())
            return it->second == "1";
    }
    return defaultValue;
}

std::string
PreferencesManager::GetVisualizerPreference(const std::string &guid) const {
    std::string section = "Device_" + guid;
    if (m_Sections.count(section)) {
        const auto &sec = m_Sections.at(section);
        auto it = sec.find("Visualizer");
        if (it != sec.end()) {
            return it->second;
        }
    }
    return "";
}

void PreferencesManager::SetVisualizerPreference(
    const std::string &guid, const std::string &visualizer) {
    m_Sections["Device_" + guid]["Visualizer"] = visualizer;
}

std::string
PreferencesManager::GetDeviceMapping(const std::string &guid) const {
    std::string section = "Device_" + guid;
    if (m_Sections.count(section)) {
        const auto &sec = m_Sections.at(section);

        // Check for legacy single-line mapping
        auto it = sec.find("Mapping");
        if (it != sec.end()) {
            return it->second;
        }

        // Construct from individual keys
        std::string mapping;
        for (const auto &pair : sec) {
            if (pair.first.compare(0, 8, "Mapping.") == 0) {
                mapping += pair.first.substr(8) + ":" + pair.second + ";";
            }
        }
        return mapping;
    }
    return "";
}

void PreferencesManager::SetDeviceMapping(const std::string &guid,
                                          const std::string &mapping) {
    std::string section = "Device_" + guid;
    m_Sections[section].erase("Mapping"); // Remove legacy key

    size_t pos = 0;
    while (pos < mapping.length()) {
        size_t end = mapping.find(';', pos);
        if (end == std::string::npos) end = mapping.length();
        std::string token = mapping.substr(pos, end - pos);
        pos = end + 1;

        size_t colon = token.find(':');
        if (colon != std::string::npos) {
            std::string key = token.substr(0, colon);
            std::string val = token.substr(colon + 1);
            m_Sections[section]["Mapping." + key] = val;
        }
    }
    Save();
}

bool PreferencesManager::IsPreferenceApplied(SDL_JoystickID instance_id) const {
    return m_AppliedPreferences.find(instance_id) != m_AppliedPreferences.end();
}

void PreferencesManager::MarkPreferenceApplied(SDL_JoystickID instance_id) {
    m_AppliedPreferences.insert(instance_id);
}

void PreferencesManager::ClearAppliedPreference(SDL_JoystickID instance_id) {
    m_AppliedPreferences.erase(instance_id);
}
