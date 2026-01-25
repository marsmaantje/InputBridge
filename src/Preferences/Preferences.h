#pragma once
#include <string>
#include <map>
#include <set>
#include <SDL3/SDL.h>

class PreferencesManager {
public:
    void Load();
    void Save();

    // Generic accessors
    void SetString(const std::string& key, const std::string& value);
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const;

    void SetInt(const std::string& key, int value);
    int GetInt(const std::string& key, int defaultValue = 0) const;

    void SetFloat(const std::string& key, float value);
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const;

    void SetBool(const std::string& key, bool value);
    bool GetBool(const std::string& key, bool defaultValue = false) const;

    // Specific accessors
    std::string GetVisualizerPreference(const std::string& guid) const;
    void SetVisualizerPreference(const std::string& guid, const std::string& visualizer);

    bool IsPreferenceApplied(SDL_JoystickID instance_id) const;
    void MarkPreferenceApplied(SDL_JoystickID instance_id);
    void ClearAppliedPreference(SDL_JoystickID instance_id);

private:
    std::map<std::string, std::string> m_Settings;
    std::set<SDL_JoystickID> m_AppliedPreferences;
    const char* CONFIG_FILENAME = "visualizer_prefs.ini";
};
