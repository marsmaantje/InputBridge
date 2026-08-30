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

    bool GetDeviceKeepalive(const std::string &guid) const;
    void SetDeviceKeepalive(const std::string &guid, bool enabled);

    // Wiimote / Balance Board settings. Keyed by HID device path rather
    // than an SDL GUID, since raw-HID Wiimote::WiimoteDevice bypasses
    // SDL_Joystick entirely and has no GUID/instance_id of its own (see
    // Devices/Wiimote/README.md). The HID path is stable across polls
    // within a session and, on the platforms this targets, is derived
    // from the underlying Bluetooth address, so it also tends to survive
    // reconnects - the best identifier available without adding our own
    // pairing/serial database.
    int  GetWiimotePlayerLED(const std::string &hid_path, int defaultValue = 1) const;
    void SetWiimotePlayerLED(const std::string &hid_path, int player_1to4);

    bool GetWiimoteIRExtendedMode(const std::string &hid_path, bool defaultValue = false) const;
    void SetWiimoteIRExtendedMode(const std::string &hid_path, bool enabled);

    // Balance Board software tare/zero (see WiimoteDevice::TareBalanceBoard).
    // Returns false (leaving outKg untouched) if no tare has ever been
    // saved for this path; a saved-but-all-zero tare and "never saved"
    // are otherwise indistinguishable, which is fine since both mean
    // "don't offset anything" to the caller.
    bool GetWiimoteBalanceTareKg(const std::string &hid_path, float outKg[4]) const;
    void SetWiimoteBalanceTareKg(const std::string &hid_path, const float kg[4]);
    void ClearWiimoteBalanceTareKg(const std::string &hid_path);

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
    static constexpr const char* kKeepaliveKey = "HapticKeepalive";
    static constexpr size_t kMappingPrefixLen = 8; // strlen("Mapping.")

    // Wiimote settings, see GetWiimotePlayerLED() etc above.
    static constexpr const char* kWiimoteSectionPrefix = "Wiimote_";
    static constexpr const char* kWiiPlayerLEDKey = "PlayerLED";
    static constexpr const char* kWiiIRExtendedKey = "IRExtendedMode";
    static constexpr const char* kWiiTareKeyPrefix = "BalanceTare"; // + 0..3
};
