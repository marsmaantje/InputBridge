#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// InputMapper is now a thin facade: it owns the four pieces the mapping
// system was split into (MappingProfileStore, InputBindingListener,
// OutputRuntimeUpdater, InputMapperUI — see src/Mappers/InputMapping/) and
// forwards every public call to one of them. This keeps the public API and
// every call site elsewhere in the codebase unchanged, while each piece of
// actual logic now lives in a small, focused, independently-readable class.
//
// InputExclusiveMode / device-hide is managed by DeviceManager, not InputMapper.
// ─────────────────────────────────────────────────────────────────────────────

#include "InputMapping/InputBindingListener.h"
#include "InputMapping/InputMapperUI.h"
#include "InputMapping/MappingProfileStore.h"
#include "InputMapping/MappingTypes.h"
#include "InputMapping/OutputRuntimeUpdater.h"

#include <memory>
#include <string>
#include <vector>

class DeviceManager;
class PreferencesManager;

class InputMapper {
  public:
    // Aliases of the InputMapping:: types, kept so existing call sites and
    // tests that spell these as InputMapper::InputSource etc. keep compiling
    // unchanged. The real definitions live in InputMapping/MappingTypes.h.
    using InputSource = InputMapping::InputSource;
    using ButtonToAnalogMapping = InputMapping::ButtonToAnalogMapping;
    using ButtonToDigitalMapping = InputMapping::ButtonToDigitalMapping;
    using AnalogToDigitalMapping = InputMapping::AnalogToDigitalMapping;
    using ChannelMix = InputMapping::ChannelMix;
    using MappingProfile = InputMapping::MappingProfile;

    static InputMapper& GetInstance();
    static void Init(const DeviceManager& deviceManager);
    static void Shutdown();

    ~InputMapper();

    InputMapper(const InputMapper&) = delete;
    InputMapper& operator=(const InputMapper&) = delete;

    void DrawContent() { m_UI.DrawContent(); }
    void DrawProfileSelector() { m_UI.DrawProfileSelector(); }
    void DrawMappingContent() { m_UI.DrawMappingContent(); }
    void DrawOutputProtocolSelector() { m_UI.DrawOutputProtocolSelector(); }
    void DrawInputProtocolSelector() { m_UI.DrawInputProtocolSelector(); }

    bool Update(bool dynamic_rate);
    std::string GetOutputPreview();

    void LoadConfig(PreferencesManager& prefs) { m_Store.LoadConfig(prefs); }
    void SaveConfig(PreferencesManager& prefs) const { m_Store.SaveConfig(prefs); }
    void SaveCurrentProfile() { m_Store.SaveCurrentProfile(); }
    void LoadProfiles() { m_Store.LoadProfiles(); }
    void SaveProfile(const MappingProfile& profile) const { m_Store.SaveProfile(profile); }
    void HandleDeviceConnectionChange() { m_Store.HandleDeviceConnectionChange(); }
    std::vector<HapticTarget>* GetCurrentHapticTargets() { return m_Store.GetCurrentHapticTargets(); }
    bool IsOutputAddressBound(const std::string& address) const { return m_Store.IsOutputAddressBound(address); }

    /**
     * @brief Atomically switch the active mapping profile.
     *
     * MappingProfile is a value type: all fields (axis mappings, haptic
     * targets, server settings, protocol IDs) are replaced wholesale on
     * activation. This prevents partial-update bugs where, for example,
     * haptic targets reflect one profile while axis mappings reflect another.
     *
     * Always prefer this over directly writing the selected index so the
     * undo/redo system, the OutputMapper, and the server settings all stay in
     * sync with a single call.
     *
     * @param index Index into the profile list, or -1 to deactivate all profiles.
     */
    void ActivateProfile(int index) { m_Store.ActivateProfile(index); }

    void CancelListening() { m_Listener.CancelListening(); }

  private:
    InputMapper(const DeviceManager& deviceManager);
    static std::unique_ptr<InputMapper> s_Instance;

    const DeviceManager& m_DeviceManager;

    InputMapping::MappingProfileStore m_Store;
    InputMapping::InputBindingListener m_Listener;
    InputMapping::OutputRuntimeUpdater m_Updater;
    InputMapping::InputMapperUI m_UI;
};
