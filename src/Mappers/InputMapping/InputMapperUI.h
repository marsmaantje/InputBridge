#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// All of InputMapper's ImGui rendering. This used to be five InputMapper
// methods, one of which (DrawMappingContent) was an ~875-line function
// covering eight visually-distinct sections (protocol selector, analog
// table, digital table, button→analog table, channel mixes, analog→digital
// table, plus three near-identical copies of a "pick a protocol definition"
// combo). Each section is now its own method below, and the three duplicated
// combo lambdas are now the one shared DrawProtocolDefinitionCombo.
// ─────────────────────────────────────────────────────────────────────────────

#include "InputBindingListener.h"
#include "MappingTypes.h"
#include <SDL3/SDL.h>
#include <string>

class DeviceManager;
struct ProtocolDefinition;
enum class ProtocolTransport;
enum class ProtocolDirection;

namespace InputMapping {

class MappingProfileStore;
class OutputRuntimeUpdater;

class InputMapperUI {
  public:
    InputMapperUI(const DeviceManager& deviceManager, MappingProfileStore& store, InputBindingListener& listener,
                  OutputRuntimeUpdater& updater);

    void DrawContent();
    void DrawProfileSelector();        // Profile selector bar only (no Begin/End)
    void DrawMappingContent();         // Mapping content only (no Begin/End)
    void DrawOutputProtocolSelector(); // Output protocol combo for the active profile
    void DrawInputProtocolSelector();  // Input protocol combo for the active profile

  private:
    const DeviceManager& m_DeviceManager;
    MappingProfileStore& m_Store;
    InputBindingListener& m_Listener;
    OutputRuntimeUpdater& m_Updater;

    char m_NewProfileName[128] = "";
    char m_RenameProfileName[128] = "";

    // ── Shared sub-widgets (previously duplicated lambdas) ─────────────────

    // The "pick a protocol definition" combo. Was a copy-pasted lambda in
    // DrawOutputProtocolSelector, DrawInputProtocolSelector, and
    // DrawMappingContent. Returns true if the selection changed; calls
    // UpdateActiveProtocols() itself on a non-"None" selection, matching all
    // three original copies. `width` matches DrawOutputProtocolSelector's
    // original copy, which fixed the combo to 200px — the other two copies
    // left it unset (auto-width), so default `width` to 0 (= don't set it).
    bool DrawProtocolDefinitionCombo(const char* label, std::string& currentId, ProtocolTransport transport,
                                      ProtocolDirection dir, float width = 0.f);

    // The OSC/WebSocket tab switcher + "(Active)"/"[Running]" indicators. Was
    // duplicated between DrawMappingContent (querying output definition ids)
    // and DrawInputProtocolSelector (querying input definition ids).
    void DrawProtocolViewSelector(const char* comboId, bool oscActive, bool wsActive, bool oscRunning,
                                   bool wsRunning, const char* tooltip, bool& changed);

    void DrawAnalogLiveBar(const InputSource& src);
    void DrawAxisCombo(const std::string& id, InputSource& src, const char* comboId, float colW, bool& changed,
                        bool showBind = true);
    void DrawButtonInputCombo(const char* comboId, SDL_JoystickID instance_id, int& button_index, int* hat_index,
                               int* hat_mask, InputSource::SensorChannel* sensor_channel, bool& changed);

    // ── DrawMappingContent's sections ───────────────────────────────────────
    void DrawAnalogOutputSection(MappingProfile& profile, const ProtocolDefinition* outDef, bool& changed);
    void DrawDigitalOutputSection(MappingProfile& profile, const ProtocolDefinition* outDef, bool& changed);
    void DrawButtonToAnalogSection(MappingProfile& profile, const ProtocolDefinition* outDef, bool& changed);
    void DrawChannelMixSection(MappingProfile& profile, const ProtocolDefinition* outDef, bool& changed);
    void DrawAnalogToDigitalSection(MappingProfile& profile, const ProtocolDefinition* outDef, bool& changed);
};

} // namespace InputMapping
