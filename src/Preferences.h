#pragma once
#include <string>
#include <map>
#include <set>
#include <SDL3/SDL.h>

class PreferencesManager {
public:
    void Load();
    void Save();

    std::string GetVisualizerPreference(const std::string& guid) const;
    void SetVisualizerPreference(const std::string& guid, const std::string& visualizer);

    bool IsPreferenceApplied(SDL_JoystickID instance_id) const;
    void MarkPreferenceApplied(SDL_JoystickID instance_id);
    void ClearAppliedPreference(SDL_JoystickID instance_id);

private:
    std::map<std::string, std::string> m_DeviceVisualizerPrefs;
    std::set<SDL_JoystickID> m_AppliedPreferences;
    const char* CONFIG_FILENAME = "visualizer_prefs.ini";
};
