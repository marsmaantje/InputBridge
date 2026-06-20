#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Owns the list of mapping profiles, which one is active, persistence
// (load/save JSON files), and pushing the active profile's settings out to
// the OSC/WebSocket servers and ProtocolManager.
//
// This is everything that used to live directly on InputMapper around
// m_Profiles / m_SelectedProfileIndex: LoadProfiles/SaveProfile,
// ActivateProfile, HandleDeviceConnectionChange, UpdateActiveProtocols,
// SnapshotServerSettings, IsOutputAddressBound, plus the profile
// create/rename/delete logic that used to live inline inside the UI's
// button handlers.
// ─────────────────────────────────────────────────────────────────────────────

#include "MappingTypes.h"
#include <string>
#include <vector>

class DeviceManager;
class PreferencesManager;
struct ProtocolDefinition;

namespace InputMapping {

class MappingProfileStore {
  public:
    explicit MappingProfileStore(const DeviceManager& deviceManager);

    // ── Persistence ──────────────────────────────────────────────────────
    void LoadProfiles();
    void SaveProfile(const MappingProfile& profile) const;
    void SaveCurrentProfile();

    void LoadConfig(PreferencesManager& prefs);
    void SaveConfig(PreferencesManager& prefs) const;

    // ── Profile lifecycle ────────────────────────────────────────────────
    // Creates a new profile named `name`, snapshots current server settings
    // into it, persists it, and activates it. Returns its index.
    int CreateProfile(const std::string& name);
    // Renames the given profile on disk and in memory. No-op if newName is
    // empty, unchanged, or index is out of range.
    bool RenameProfile(int index, const std::string& newName);
    // Removes the profile's JSON file and erases it from memory. Deactivates
    // the active profile afterwards (matching the original UI behaviour).
    void DeleteProfile(int index);

    // Atomically switches the active profile (or deactivates with index -1).
    // See the original InputMapper::ActivateProfile doc comment: this keeps
    // OutputMapper's haptic targets, the OSC/WS servers' protocol selection,
    // and the per-profile UI state all in sync.
    void ActivateProfile(int index);

    // Remaps every profile's stored device GUIDs to the current session's
    // SDL_JoystickIDs (which change across reconnects/relaunches).
    void HandleDeviceConnectionChange();

    // Pushes the active profile's protocol IDs and server settings out to
    // the OSC/WebSocket servers and ProtocolManager.
    void UpdateActiveProtocols();

    // Reads the OSC/WS servers' *current* settings back into `profile`
    // (used right before saving, and when creating a new profile so it
    // inherits the currently-running server configuration).
    void SnapshotServerSettings(MappingProfile& profile) const;

    // ── Accessors ────────────────────────────────────────────────────────
    std::vector<MappingProfile>&       Profiles()       { return m_Profiles; }
    const std::vector<MappingProfile>& Profiles() const { return m_Profiles; }

    int SelectedIndex() const { return m_SelectedProfileIndex; }

    MappingProfile*       CurrentProfile();
    const MappingProfile* CurrentProfile() const;

    int  SelectedProtocolView() const { return m_SelectedProtocolView; }
    void SetSelectedProtocolView(int view) { m_SelectedProtocolView = view; }

    // Returns the active profile's haptic target list, or nullptr if no
    // profile is active. Ownership stays with the profile/this store.
    std::vector<::HapticTarget>* GetCurrentHapticTargets();

    // Resolves which ProtocolDefinition is currently being edited/previewed
    // for the active profile + selected protocol view (OSC vs WebSocket).
    const ProtocolDefinition* GetActiveOutputDefinition() const;

    // True if `address` (an OSC path or WebSocket key) is bound to a device
    // input anywhere in the active profile.
    bool IsOutputAddressBound(const std::string& address) const;

  private:
    const DeviceManager& m_DeviceManager;
    std::vector<MappingProfile> m_Profiles;
    int m_SelectedProfileIndex = -1;
    int m_SelectedProtocolView = 0; // 0: OSC, 1: WebSocket
};

} // namespace InputMapping
