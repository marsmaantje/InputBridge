#pragma once
#include <SDL3/SDL.h>
#include <map>
#include <set>
#include <string>

namespace PrefKeys {
    // UI
    inline constexpr const char* UIScale = "UIScale";
    inline constexpr const char* FontScale = "FontScale";
    inline constexpr const char* ScaleWithWindow = "ScaleWithWindow";

    // Network
    inline constexpr const char* NetworkSection = "Network";
    inline constexpr const char* UpdateRate = "UpdateRate";
    inline constexpr const char* DynamicRate = "DynamicRate";
}

class PreferencesManager {
  public:
    void Load();
    void Save();

    // Generic accessors
    void SetString(const std::string &key, const std::string &value);
    std::string GetString(const std::string &key, const std::string &defaultValue = "") const;
    void SetString(const std::string &section, const std::string &key, const std::string &value);
    std::string GetString(const std::string &section, const std::string &key, const std::string &defaultValue) const;
    void DeleteKey(const std::string &key);
    void DeleteKey(const std::string &section, const std::string &key);

    void SetInt(const std::string &key, int value);
    int GetInt(const std::string &key, int defaultValue = 0) const;
    void SetInt(const std::string &section, const std::string &key, int value);
    int GetInt(const std::string &section, const std::string &key, int defaultValue) const;

    void SetFloat(const std::string &key, float value);
    float GetFloat(const std::string &key, float defaultValue = 0.0f) const;
    void SetFloat(const std::string &section, const std::string &key, float value);
    float GetFloat(const std::string &section, const std::string &key, float defaultValue) const;

    void SetBool(const std::string &key, bool value);
    bool GetBool(const std::string &key, bool defaultValue = false) const;
    void SetBool(const std::string &section, const std::string &key, bool value);
    bool GetBool(const std::string &section, const std::string &key, bool defaultValue) const;

    // Specific accessors
    std::string GetVisualizerPreference(const std::string &guid) const;
    void SetVisualizerPreference(const std::string &guid, const std::string &visualizer);

    std::string GetDeviceMapping(const std::string &guid) const;
    void SetDeviceMapping(const std::string &guid, const std::string &mapping);

    bool IsPreferenceApplied(SDL_JoystickID instance_id) const;
    void MarkPreferenceApplied(SDL_JoystickID instance_id);
    void ClearAppliedPreference(SDL_JoystickID instance_id);

  private:
    std::map<std::string, std::map<std::string, std::string>> m_Sections;
    std::set<SDL_JoystickID> m_AppliedPreferences;
    const char *CONFIG_FILENAME = "visualizer_prefs.toml";
    std::string GetConfigFilePath();

    // Constants for sections and keys
    static constexpr const char* kGeneralSection = "General";
    static constexpr const char* kDeviceSectionPrefix = "Device_";
    static constexpr const char* kVisualizerKey = "Visualizer";
    static constexpr const char* kMappingKey = "Mapping";
    static constexpr const char* kMappingPrefix = "Mapping.";
    static constexpr size_t kMappingPrefixLen = 8; // strlen("Mapping.")
};
