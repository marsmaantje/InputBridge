#include "Preferences.h"
#include <fstream>

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
                    m_DeviceVisualizerPrefs[guid] = viz;
                }
            }
        }
        file.close();
    }
}

void PreferencesManager::Save() {
    std::ofstream file(CONFIG_FILENAME);
    if (file.is_open()) {
        for (const auto& pair : m_DeviceVisualizerPrefs) {
            file << pair.first << "=" << pair.second << "\n";
        }
        file.close();
    }
}

std::string PreferencesManager::GetVisualizerPreference(const std::string& guid) const {
    auto it = m_DeviceVisualizerPrefs.find(guid);
    if (it != m_DeviceVisualizerPrefs.end()) {
        return it->second;
    }
    return "";
}

void PreferencesManager::SetVisualizerPreference(const std::string& guid, const std::string& visualizer) {
    m_DeviceVisualizerPrefs[guid] = visualizer;
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
