#include "Preferences.h"
#include <fstream>
#include <cstdlib>

void PreferencesManager::Load() {
    std::ifstream file(CONFIG_FILENAME);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string guid = line.substr(0, eqPos);
                std::string viz = line.substr(eqPos + 1);
                if (!guid.empty() && !viz.empty()) {
                    m_Settings[guid] = viz;
                }
            }
        }
        file.close();
    }
}

void PreferencesManager::Save() {
    std::ofstream file(CONFIG_FILENAME);
    if (file.is_open()) {
        for (const auto& pair : m_Settings) {
            file << pair.first << "=" << pair.second << "\n";
        }
        file.close();
    }
}

void PreferencesManager::SetString(const std::string& key, const std::string& value) {
    m_Settings[key] = value;
}

std::string PreferencesManager::GetString(const std::string& key, const std::string& defaultValue) const {
    auto it = m_Settings.find(key);
    if (it != m_Settings.end()) return it->second;
    return defaultValue;
}

void PreferencesManager::SetInt(const std::string& key, int value) {
    m_Settings[key] = std::to_string(value);
}

int PreferencesManager::GetInt(const std::string& key, int defaultValue) const {
    auto it = m_Settings.find(key);
    if (it != m_Settings.end()) return std::atoi(it->second.c_str());
    return defaultValue;
}

void PreferencesManager::SetFloat(const std::string& key, float value) {
    m_Settings[key] = std::to_string(value);
}

float PreferencesManager::GetFloat(const std::string& key, float defaultValue) const {
    auto it = m_Settings.find(key);
    if (it != m_Settings.end()) return (float)std::atof(it->second.c_str());
    return defaultValue;
}

void PreferencesManager::SetBool(const std::string& key, bool value) {
    m_Settings[key] = value ? "1" : "0";
}

bool PreferencesManager::GetBool(const std::string& key, bool defaultValue) const {
    auto it = m_Settings.find(key);
    if (it != m_Settings.end()) return it->second == "1";
    return defaultValue;
}

std::string PreferencesManager::GetVisualizerPreference(const std::string& guid) const {
    auto it = m_Settings.find(guid);
    if (it != m_Settings.end()) {
        return it->second;
    }
    return "";
}

void PreferencesManager::SetVisualizerPreference(const std::string& guid, const std::string& visualizer) {
    m_Settings[guid] = visualizer;
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
