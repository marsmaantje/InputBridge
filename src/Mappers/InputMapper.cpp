#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "Mappers/OutputMapper.h"
#include "Network/WebSocketServer.h"
#include "Network/OSCServer.h"
#include "Protocols/ProtocolRegistry.h"
#include "Protocols/ProtocolDefinition.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/OSCSteamLinkProtocol.h"
#include "Protocols/OSCProjectBabbleProtocol.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <memory>
#include <SDL3/SDL_filesystem.h>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

NLOHMANN_JSON_SERIALIZE_ENUM(InputMapper::ButtonToDigitalMapping::Mode, {
    {InputMapper::ButtonToDigitalMapping::Mode::Momentary, "momentary"},
    {InputMapper::ButtonToDigitalMapping::Mode::Toggle,    "toggle"},
    {InputMapper::ButtonToDigitalMapping::Mode::SetOn,     "set_on"},
    {InputMapper::ButtonToDigitalMapping::Mode::SetOff,    "set_off"},
})

std::unique_ptr<InputMapper> InputMapper::s_Instance;

InputMapper &InputMapper::GetInstance() { return *s_Instance; }

void InputMapper::Init(const DeviceManager &deviceManager) {
    if (!s_Instance) s_Instance.reset(new InputMapper(deviceManager));
}

void InputMapper::Shutdown() { s_Instance.reset(); }

namespace {

SDL_Joystick *GetJoystickByID(SDL_JoystickID id, const DeviceManager &dm) {
    if (id == 0) return nullptr;
    for (const auto &d : dm.GetDevices())
        if (d.instance_id == id) return d.joystick;
    return nullptr;
}

std::filesystem::path GetMappingsDirectory() {
    const char *base = SDL_GetBasePath();
    return base ? std::filesystem::path(base) / "mappings" : std::filesystem::path("mappings");
}

const FieldDescriptor* FindFieldDescriptor(const std::string& id) {
    for (const auto& fd : ProtocolRegistry::GetInstance().GetOutputFields())
        if (fd.id == id) return &fd;
    for (const auto& fd : ProtocolRegistry::GetInstance().GetInputFields())
        if (fd.id == id) return &fd;
    return nullptr;
}

using FieldPairs = std::vector<std::pair<const ProtocolField*, const FieldDescriptor*>>;
FieldPairs GetEnabledFields(const ProtocolDefinition& def, FieldType type) {
    FieldPairs result;
    for (const auto& pf : def.fields) {
        if (!pf.enabled) continue;
        const auto* fd = FindFieldDescriptor(pf.fieldId);
        if (fd && fd->type == type) result.push_back({&pf, fd});
    }
    return result;
}

} // namespace

InputMapper::InputMapper(const DeviceManager &dm) : m_DeviceManager(dm) {}
InputMapper::~InputMapper() {}

const ProtocolDefinition* InputMapper::GetActiveOutputDefinition() {
    std::string oscId;
    std::string wsId;

    if (m_SelectedProfileIndex != -1) {
        const auto& p = m_Profiles[m_SelectedProfileIndex];
        oscId = p.oscOutputProtocolId;
        wsId = p.wsOutputProtocolId;
    } else {
        // Fallback or default behavior when no profile is selected
    }

    if (m_SelectedProtocolView == 0) { // OSC
        if (!oscId.empty()) return ProtocolRegistry::GetInstance().FindById(oscId);

        std::string protocol = OSCServer::GetInstance().GetProtocol();
        if (protocol == "SteamLink OSC") {
            static ProtocolDefinition s_steamLinkDef;
            s_steamLinkDef = OSCSteamLinkProtocol::CreateDefaultDefinition();
            return &s_steamLinkDef;
        }
        if (protocol == "Project Babble OSC") {
            static ProtocolDefinition s_projectBabbleDef;
            s_projectBabbleDef = OSCProjectBabbleProtocol::CreateDefaultDefinition();
            return &s_projectBabbleDef;
        }
    } else { // WebSocket
        if (!wsId.empty()) return ProtocolRegistry::GetInstance().FindById(wsId);
    }
    return nullptr;
}

void InputMapper::LoadConfig(PreferencesManager &prefs) {
    // Null the OutputMapper pointer before LoadProfiles() calls m_Profiles.clear()
    // followed by many push_backs. Any of those reallocations would leave the
    // OutputMapper holding a dangling pointer into the old buffer.
    OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
    LoadProfiles();
    std::string last = prefs.GetString("InputMapper", "LastProfile", "");
    for (int i = 0; i < (int)m_Profiles.size(); ++i)
        if (m_Profiles[i].name == last) { m_SelectedProfileIndex = i; break; }
    if (m_SelectedProfileIndex != -1) {
        OutputMapper::GetInstance().SetActiveHapticTargets(&m_Profiles[m_SelectedProfileIndex].hapticTargets);
        const auto& p = m_Profiles[m_SelectedProfileIndex];
        m_SelectedProtocolView = p.selectedProtocolView;
    } else {
        m_SelectedProtocolView = prefs.GetInt("InputMapper", "SelectedProtocolView", 0);
    }
    UpdateActiveProtocols();
    HandleDeviceConnectionChange();
}

void InputMapper::SaveConfig(PreferencesManager &prefs) const {
    prefs.SetString("InputMapper", "LastProfile",
        m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < (int)m_Profiles.size()
            ? m_Profiles[m_SelectedProfileIndex].name : "");
    prefs.SetInt("InputMapper", "SelectedProtocolView", m_SelectedProtocolView);
}

void InputMapper::SaveCurrentProfile() const {
    if (m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < (int)m_Profiles.size()) {
        // Const-cast safe: snapshot only reads from servers and writes to a
        // profile copy for persistence — it does not mutate any observable state.
        auto& profile = const_cast<MappingProfile&>(m_Profiles[m_SelectedProfileIndex]);
        const_cast<InputMapper*>(this)->SnapshotServerSettings(profile);
        SaveProfile(profile);
    }
}

void InputMapper::CancelListening() {
    m_ListeningState.active = false;
    m_ListeningState.initialAxes.clear();
}

void InputMapper::StartListening(ListeningState::Type type, const std::string& name, int index) {
    m_ListeningState.active = true;
    m_ListeningState.type = type;
    m_ListeningState.targetName = name;
    m_ListeningState.listIndex = index;
    m_ListeningState.initialAxes.clear();

    if (type == ListeningState::Axis) {
        for (const auto& dev : m_DeviceManager.GetDevices()) {
            if (!dev.joystick) continue;
            for (int i = 0; i < dev.num_axes; ++i) {
                m_ListeningState.initialAxes.push_back({dev.instance_id, i, SDL_GetJoystickAxis(dev.joystick, i)});
            }
        }
    }
}

void InputMapper::UpdateListening() {
    if (!m_ListeningState.active) return;
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) {
        CancelListening();
        return;
    }
    MappingProfile &profile = m_Profiles[m_SelectedProfileIndex];
    const auto& devices = m_DeviceManager.GetDevices();

    if (m_ListeningState.type == ListeningState::Axis) {
        for (const auto& dev : devices) {
            if (!dev.joystick) continue;
            for (int i = 0; i < dev.num_axes; ++i) {
                Sint16 val = SDL_GetJoystickAxis(dev.joystick, i);
                Sint16 baseline = 0;
                bool found = false;
                for (const auto& as : m_ListeningState.initialAxes) {
                    if (as.instance_id == dev.instance_id && as.axis_index == i) {
                        baseline = as.value;
                        found = true;
                        break;
                    }
                }
                if (found && std::abs((int)val - (int)baseline) > 10000) {
                    InputSource& src = profile.outputToInput[m_ListeningState.targetName];
                    src.deviceGuid = DeviceManager::GetDeviceGUIDString(dev);
                    src.instance_id = dev.instance_id;
                    src.axisIndex = i;
                    SaveCurrentProfile();
                    CancelListening();
                    return;
                }
            }
        }
    } else if (m_ListeningState.type == ListeningState::Digital) {
        for (const auto& dev : devices) {
            if (!dev.joystick) continue;
            for (int i = 0; i < dev.num_buttons; ++i) {
                if (SDL_GetJoystickButton(dev.joystick, i)) {
                    bool updated = false;
                    if (m_ListeningState.targetName == "digital") {
                        if (m_ListeningState.listIndex >= 0 && m_ListeningState.listIndex < (int)profile.digitalMappings.size()) {
                            auto& dm = profile.digitalMappings[m_ListeningState.listIndex];
                            dm.device_guid = DeviceManager::GetDeviceGUIDString(dev);
                            dm.instance_id = dev.instance_id;
                            dm.button_index = i;
                            dm.hat_index = -1;
                            dm.hat_mask = 0;
                            updated = true;
                        }
                    } else if (m_ListeningState.targetName == "button_analog") {
                        if (m_ListeningState.listIndex >= 0 && m_ListeningState.listIndex < (int)profile.buttonMappings.size()) {
                            auto& bm = profile.buttonMappings[m_ListeningState.listIndex];
                            bm.device_guid = DeviceManager::GetDeviceGUIDString(dev);
                            bm.instance_id = dev.instance_id;
                            bm.button_index = i;
                            updated = true;
                        }
                    }
                    if (updated) SaveCurrentProfile();
                    CancelListening();
                    return;
                }
            }
            // Check Hats
            for (int i = 0; i < dev.num_hats; ++i) {
                Uint8 hat = SDL_GetJoystickHat(dev.joystick, i);
                if (hat != SDL_HAT_CENTERED) {
                    bool updated = false;
                    if (m_ListeningState.targetName == "digital") {
                        if (m_ListeningState.listIndex >= 0 && m_ListeningState.listIndex < (int)profile.digitalMappings.size()) {
                            auto& dm = profile.digitalMappings[m_ListeningState.listIndex];
                            dm.device_guid = DeviceManager::GetDeviceGUIDString(dev);
                            dm.instance_id = dev.instance_id;
                            dm.button_index = -1;
                            dm.hat_index = i;
                            dm.hat_mask = hat;
                            updated = true;
                        }
                    }
                    // Note: ButtonToAnalogMapping doesn't support hats yet in this implementation
                    if (updated) SaveCurrentProfile();
                    CancelListening();
                    return;
                }
            }
        }
    }
}

void InputMapper::DrawContent() {
    ImGui::Begin("Input Mapper");
    DrawProfileSelector();   // calls HandleDeviceConnectionChange + UpdateListening
    DrawMappingContent();
    ImGui::End();
}

void InputMapper::DrawProfileSelector() {
    HandleDeviceConnectionChange();
    UpdateListening();

    // Profile management
    ImGui::Text("Mapping Profiles");
    ImGui::Separator();

    int old_idx = m_SelectedProfileIndex;
    const char *cur = m_SelectedProfileIndex != -1 ? m_Profiles[m_SelectedProfileIndex].name.c_str() : "None";
    if (ImGui::BeginCombo("Active Profile", cur)) {
        if (ImGui::Selectable("None", m_SelectedProfileIndex == -1)) m_SelectedProfileIndex = -1;
        for (int i = 0; i < (int)m_Profiles.size(); ++i) {
            if (ImGui::Selectable(m_Profiles[i].name.c_str(), m_SelectedProfileIndex == i))
                m_SelectedProfileIndex = i;
            if (m_SelectedProfileIndex == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (old_idx != m_SelectedProfileIndex) {
        OutputMapper::GetInstance().SetActiveHapticTargets(
            m_SelectedProfileIndex != -1 ? &m_Profiles[m_SelectedProfileIndex].hapticTargets : nullptr);
        if (m_SelectedProfileIndex != -1) {
            const auto& p = m_Profiles[m_SelectedProfileIndex];
            m_SelectedProtocolView = p.selectedProtocolView;
        } else {
        }
        UpdateActiveProtocols();
    }

    {
        float avail = ImGui::GetContentRegionAvail().x;
        float spc   = ImGui::GetStyle().ItemSpacing.x;
        float cw    = ImGui::CalcTextSize("Create New").x + ImGui::GetStyle().FramePadding.x * 2;
        float renw  = ImGui::CalcTextSize("Rename").x     + ImGui::GetStyle().FramePadding.x * 2;
        float dw    = ImGui::CalcTextSize("Delete").x    + ImGui::GetStyle().FramePadding.x * 2;
        float iw    = avail - cw - spc - (m_SelectedProfileIndex != -1 ? dw + spc + renw + spc : 0);
        ImGui::SetNextItemWidth(std::max(1.f, iw));
        ImGui::InputTextWithHint("##npn", "New Profile Name", m_NewProfileName, sizeof(m_NewProfileName));
        ImGui::SameLine();
        if (ImGui::Button("Create New") && strlen(m_NewProfileName) > 0) {
            MappingProfile p; p.name = m_NewProfileName;
            SnapshotServerSettings(p);
            // Null the pointer BEFORE push_back: if the vector reallocates its
            // buffer, SetActiveHapticTargets would otherwise iterate through
            // the old (now freed) memory when closing the previous targets.
            OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
            m_Profiles.push_back(p);
            m_SelectedProfileIndex = (int)m_Profiles.size() - 1;
            SaveProfile(p);
            OutputMapper::GetInstance().SetActiveHapticTargets(&m_Profiles.back().hapticTargets);
            m_NewProfileName[0] = '\0';
        }
        if (m_SelectedProfileIndex != -1) {
            ImGui::SameLine();
            if (ImGui::Button("Rename")) {
                std::strncpy(m_RenameProfileName, m_Profiles[m_SelectedProfileIndex].name.c_str(), sizeof(m_RenameProfileName));
                m_RenameProfileName[sizeof(m_RenameProfileName)-1] = '\0';
                ImGui::OpenPopup("Rename Profile");
            }
            if (ImGui::BeginPopupModal("Rename Profile", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::InputText("New Name", m_RenameProfileName, sizeof(m_RenameProfileName));
                if (ImGui::Button("Save", ImVec2(120, 0))) {
                    std::string newName = m_RenameProfileName;
                    std::string oldName = m_Profiles[m_SelectedProfileIndex].name;
                    if (!newName.empty() && newName != oldName) {
                        auto dir = GetMappingsDirectory();
                        auto oldPath = dir / (oldName + ".json");
                        auto newPath = dir / (newName + ".json");
                        m_Profiles[m_SelectedProfileIndex].name = newName;
                        SaveProfile(m_Profiles[m_SelectedProfileIndex]);
                        std::error_code ec;
                        if (std::filesystem::exists(oldPath, ec)) {
                            if (!std::filesystem::exists(newPath, ec) || !std::filesystem::equivalent(oldPath, newPath, ec))
                                std::filesystem::remove(oldPath, ec);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete Profile?");
            if (ImGui::BeginPopupModal("Delete Profile?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete '%s'?", m_Profiles[m_SelectedProfileIndex].name.c_str());
                ImGui::Separator();
                if (ImGui::Button("Yes", ImVec2(120,0))) {
                    try {
                        auto p = GetMappingsDirectory() / (m_Profiles[m_SelectedProfileIndex].name + ".json");
                        if (std::filesystem::exists(p)) std::filesystem::remove(p);
                    } catch (...) {}
                    // Null the pointer BEFORE erase: erase invalidates all
                    // iterators and pointers into the vector, so
                    // SetActiveHapticTargets must not dereference the old
                    // pointer when closing haptic devices on the deleted profile.
                    OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
                    m_Profiles.erase(m_Profiles.begin() + m_SelectedProfileIndex);
                    m_SelectedProfileIndex = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(120,0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
    }
}

void InputMapper::DrawOutputProtocolSelector() {
    if (m_SelectedProfileIndex == -1) return;
    bool changed = false;
    MappingProfile& profile = m_Profiles[m_SelectedProfileIndex];

    auto drawProtoCombo = [&](const char* label, std::string& currentId, ProtocolTransport transport, ProtocolDirection dir) {
        std::string preview = "None";
        if (!currentId.empty()) {
            if (auto* def = ProtocolRegistry::GetInstance().FindById(currentId)) preview = def->name;
            else preview = "Unknown (" + currentId + ")";
        }
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo(label, preview.c_str())) {
            if (ImGui::Selectable("None", currentId.empty())) { currentId = ""; changed = true; }
            for (const auto& def : ProtocolRegistry::GetInstance().GetDefinitions()) {
                if (def.transport == transport && def.direction == dir) {
                    if (ImGui::Selectable(def.name.c_str(), def.id == currentId)) {
                        currentId = def.id; changed = true; UpdateActiveProtocols();
                    }
                }
            }
            ImGui::EndCombo();
        }
    };

    if (m_SelectedProtocolView == 0) {
        drawProtoCombo("OSC Output##out", profile.oscOutputProtocolId, ProtocolTransport::OSC, ProtocolDirection::Output);
    } else {
#ifdef ENABLE_WEBSOCKETS
        drawProtoCombo("WS Output##out", profile.wsOutputProtocolId, ProtocolTransport::WebSocket, ProtocolDirection::Output);
#else
        ImGui::TextDisabled("WebSockets disabled.");
#endif
    }
    if (changed) SaveProfile(profile);
}

void InputMapper::DrawInputProtocolSelector() {
    if (m_SelectedProfileIndex == -1) return;
    bool changed = false;
    MappingProfile& profile = m_Profiles[m_SelectedProfileIndex];

    // ── Active Protocol Selection ─────────────────────────────────────────────
    {
        bool oscActive  = !OSCServer::GetInstance().GetInputDefinitionId().empty();
        bool oscRunning = OSCServer::GetInstance().IsRunning();
#ifdef ENABLE_WEBSOCKETS
        bool wsActive  = !WebSocketServer::GetInstance().GetInputDefinitionId().empty();
        bool wsRunning = WebSocketServer::GetInstance().IsRunning();
#else
        bool wsActive  = false;
        bool wsRunning = false;
#endif
        ImGui::SetNextItemWidth(120);
        int oldView = m_SelectedProtocolView;
        if (ImGui::BeginCombo("##protoview_in", m_SelectedProtocolView == 0 ? "OSC" : "WebSocket")) {
            if (ImGui::Selectable("OSC", m_SelectedProtocolView == 0)) m_SelectedProtocolView = 0;
            if (oscActive) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0,1,0,1), "(Active)"); }
            if (ImGui::Selectable("WebSocket", m_SelectedProtocolView == 1)) m_SelectedProtocolView = 1;
            if (wsActive) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0,1,0,1), "(Active)"); }
            ImGui::EndCombo();
        }
        if (oldView != m_SelectedProtocolView && m_SelectedProfileIndex != -1) {
            m_Profiles[m_SelectedProfileIndex].selectedProtocolView = m_SelectedProtocolView;
            changed = true;
        }

        ImGui::SameLine();
        bool isRunning = (m_SelectedProtocolView == 0) ? oscRunning : wsRunning;
        if (isRunning) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[Running]");
        else           ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[Stopped]");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Select which protocol definition to use for incoming data.\nOnly one definition drives the output mapping at a time.");
    }

    auto drawProtoCombo = [&](const char* label, std::string& currentId, ProtocolTransport transport, ProtocolDirection dir) {
        std::string preview = "None";
        if (!currentId.empty()) {
            if (auto* def = ProtocolRegistry::GetInstance().FindById(currentId)) preview = def->name;
            else preview = "Unknown (" + currentId + ")";
        }
        if (ImGui::BeginCombo(label, preview.c_str())) {
            if (ImGui::Selectable("None", currentId.empty())) { currentId = ""; changed = true; }
            for (const auto& def : ProtocolRegistry::GetInstance().GetDefinitions()) {
                if (def.transport == transport && def.direction == dir) {
                    if (ImGui::Selectable(def.name.c_str(), def.id == currentId)) { currentId = def.id; changed = true; UpdateActiveProtocols(); }
                }
            }
            ImGui::EndCombo();
        }
    };

    if (m_SelectedProtocolView == 0) {
        drawProtoCombo("OSC Input", profile.oscInputProtocolId, ProtocolTransport::OSC, ProtocolDirection::Input);
    } else {
#ifdef ENABLE_WEBSOCKETS
        drawProtoCombo("WebSocket Input", profile.wsInputProtocolId, ProtocolTransport::WebSocket, ProtocolDirection::Input);
#else
        ImGui::TextDisabled("WebSockets are disabled in this build.");
#endif
    }

    if (changed) SaveProfile(profile);
}

void InputMapper::DrawMappingContent() {
    ImGui::Separator();
    if (m_SelectedProfileIndex == -1) { ImGui::TextDisabled("Select or create a profile above."); return; }

    bool changed = false;
    MappingProfile &profile = m_Profiles[m_SelectedProfileIndex];

    // ── Active Protocol Selection (top of page) ───────────────────────────────
    {
        bool oscActive  = !OSCServer::GetInstance().GetOutputDefinitionId().empty();
        bool oscRunning = OSCServer::GetInstance().IsRunning();
#ifdef ENABLE_WEBSOCKETS
        bool wsActive  = !WebSocketServer::GetInstance().GetOutputDefinitionId().empty();
        bool wsRunning = WebSocketServer::GetInstance().IsRunning();
#else
        bool wsActive  = false;
        bool wsRunning = false;
#endif
        ImGui::SetNextItemWidth(120);
        int oldView = m_SelectedProtocolView;
        if (ImGui::BeginCombo("##protoview", m_SelectedProtocolView == 0 ? "OSC" : "WebSocket")) {
            if (ImGui::Selectable("OSC", m_SelectedProtocolView == 0)) m_SelectedProtocolView = 0;
            if (oscActive) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0,1,0,1), "(Active)"); }
            if (ImGui::Selectable("WebSocket", m_SelectedProtocolView == 1)) m_SelectedProtocolView = 1;
            if (wsActive) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0,1,0,1), "(Active)"); }
            ImGui::EndCombo();
        }
        if (oldView != m_SelectedProtocolView && m_SelectedProfileIndex != -1) {
            m_Profiles[m_SelectedProfileIndex].selectedProtocolView = m_SelectedProtocolView;
            changed = true;
        }

        ImGui::SameLine();
        bool isRunning = (m_SelectedProtocolView == 0) ? oscRunning : wsRunning;
        if (isRunning) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[Running]");
        else           ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[Stopped]");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Select which protocol definition to use for mapping and preview.\nOnly one definition drives the input mapping at a time.");
    }

    auto drawProtoCombo = [&](const char* label, std::string& currentId, ProtocolTransport transport, ProtocolDirection dir) {
        std::string preview = "None";
        if (!currentId.empty()) {
            if (auto* def = ProtocolRegistry::GetInstance().FindById(currentId)) preview = def->name;
            else preview = "Unknown (" + currentId + ")";
        }
        if (ImGui::BeginCombo(label, preview.c_str())) {
            if (ImGui::Selectable("None", currentId.empty())) { currentId = ""; changed = true; }
            for (const auto& def : ProtocolRegistry::GetInstance().GetDefinitions()) {
                if (def.transport == transport && def.direction == dir) {
                    if (ImGui::Selectable(def.name.c_str(), def.id == currentId)) { currentId = def.id; changed = true; UpdateActiveProtocols(); }
                }
            }
            ImGui::EndCombo();
        }
    };

    if (m_SelectedProtocolView == 0) {
        drawProtoCombo("OSC Output", profile.oscOutputProtocolId, ProtocolTransport::OSC, ProtocolDirection::Output);
    } else {
#ifdef ENABLE_WEBSOCKETS
        drawProtoCombo("WebSocket Output", profile.wsOutputProtocolId, ProtocolTransport::WebSocket, ProtocolDirection::Output);
#else
        ImGui::TextDisabled("WebSockets are disabled in this build.");
#endif
    }

    ImGui::Separator();

    ImGui::Text("'%s' Mappings", profile.name.c_str());

    const ProtocolDefinition* outDef = GetActiveOutputDefinition();

    // Axis combo helper
    auto drawAxisCombo = [&](const std::string& id, InputSource& src, const char* comboId, float colW) {
        std::string preview = "None";
        if (src.instance_id != 0)
            for (const auto& d : m_DeviceManager.GetDevices())
                if (d.instance_id == src.instance_id) { preview = d.name + " - Axis " + std::to_string(src.axisIndex); break; }

        ImGuiStyle& style = ImGui::GetStyle();
        float sp = style.ItemSpacing.x;
        float bindW = ImGui::CalcTextSize("Bind").x + style.FramePadding.x * 2;

        float dw = 0.f, rw = 0.f;
        bool hasSrc = src.axisIndex != -1;
        if (hasSrc) {
            dw = 80.f;
            rw = 80.f;
        }

        ImGui::SetNextItemWidth(std::max(1.0f, colW - sp - bindW));

        if (ImGui::BeginCombo(comboId, preview.c_str())) {
            if (ImGui::Selectable("None", !hasSrc)) { src = {}; changed = true; }
            for (const auto& dev : m_DeviceManager.GetDevices())
                for (int i = 0; i < dev.num_axes; ++i) {
                    std::string lbl = dev.name + " - Axis " + std::to_string(i);
                    bool sel = src.instance_id == dev.instance_id && src.axisIndex == i;
                    if (ImGui::Selectable(lbl.c_str(), sel)) {
                        src.deviceGuid  = DeviceManager::GetDeviceGUIDString(dev);
                        src.instance_id = dev.instance_id;
                        src.axisIndex   = i;
                        changed = true;
                    }
                }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        bool isListening = m_ListeningState.active && m_ListeningState.type == ListeningState::Axis && m_ListeningState.targetName == id;
        if (isListening) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
            if (ImGui::Button("Waiting...")) CancelListening();
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Waiting for input... Click to cancel.");
        } else {
            if (ImGui::Button("Bind")) StartListening(ListeningState::Axis, id);
            ImGui::SetItemTooltip("Click to bind an axis.");
        }

        if (hasSrc) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("X")) { src = {}; changed = true; }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Clear");
            ImGui::SameLine();
            if (ImGui::Checkbox("Inv", &src.invert)) changed = true;
            ImGui::SetItemTooltip("Invert");
            ImGui::SameLine(); ImGui::SetNextItemWidth(dw);
            if (ImGui::SliderFloat("DZ", &src.deadzone, 0.f, 0.5f, "%.3f")) changed = true;
            ImGui::SetItemTooltip("Deadzone");
            ImGui::SameLine(); ImGui::SetNextItemWidth(rw);
            const char* ranges[] = {"-1..1","0..1","-1..0"};
            if (ImGui::Combo("Range", &src.outputRange, ranges, 3)) changed = true;
        }
    };

    // ── Analog output channels ────────────────────────────────────────────────
    ImGui::Spacing();
    if (outDef) {
        auto analogFields = GetEnabledFields(*outDef, FieldType::AnalogAxis);
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.f,1.f), "Analog Output Channels  (%s)", outDef->name.c_str());
        if (analogFields.empty()) {
            ImGui::TextDisabled("No enabled analog fields in this protocol.");
        } else if (ImGui::BeginTable("t_analog", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Field",       ImGuiTableColumnFlags_WidthFixed, 170.f);
            ImGui::TableSetupColumn("Device Axis", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (auto& [pf, fd] : analogFields) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", fd->label.c_str());
                ImGui::SetItemTooltip("OSC: %s   WS: %s", pf->oscPath.c_str(), pf->wsKey.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(("a_" + pf->fieldId).c_str());
                drawAxisCombo(pf->fieldId, profile.outputToInput[pf->fieldId], "##ax", ImGui::GetContentRegionAvail().x);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextColored(ImVec4(0.8f,0.8f,0.4f,1.f), "Analog Output Channels  (legacy – no protocol selected)");
        if (ImGui::BeginTable("t_legacy", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Output",      ImGuiTableColumnFlags_WidthFixed, 120.f);
            ImGui::TableSetupColumn("Device Axis", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& name : m_GenericOutputs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(name.c_str());
                drawAxisCombo(name, profile.outputToInput[name], "##ax", ImGui::GetContentRegionAvail().x);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // ── Digital output channels ───────────────────────────────────────────────
    if (outDef) {
        auto digitalFields = GetEnabledFields(*outDef, FieldType::DigitalButton);
        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,1.f,0.6f,1.f), "Digital Output Channels  (%s)", outDef->name.c_str());

        if (digitalFields.empty()) {
            ImGui::TextDisabled("No enabled digital fields in this protocol.");
        } else {
            ImGui::TextWrapped("Bind device buttons to digital (0/1) output fields.");
            if (ImGui::Button("Add Digital Mapping")) { profile.digitalMappings.push_back({}); changed = true; }

            int toDelete = -1;
            if (ImGui::BeginTable("t_digital", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Device",        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Button Index",  ImGuiTableColumnFlags_WidthFixed, 90.f);
                ImGui::TableSetupColumn("Digital Field", ImGuiTableColumnFlags_WidthFixed, 150.f);
                ImGui::TableSetupColumn("Mode",          ImGuiTableColumnFlags_WidthFixed, 90.f);
                ImGui::TableSetupColumn("",              ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (int i = 0; i < (int)profile.digitalMappings.size(); ++i) {
                    auto& dm = profile.digitalMappings[i];
                    bool rc = false;
                    ImGui::PushID(5000 + i); ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    std::string dname = "None";
                    for (const auto& d : m_DeviceManager.GetDevices())
                        if (d.instance_id == dm.instance_id) { dname = d.name; break; }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##dd", dname.c_str())) {
                        if (ImGui::Selectable("None", dm.instance_id==0)) { dm.device_guid=""; dm.instance_id=0; rc=true; }
                        for (const auto& d : m_DeviceManager.GetDevices())
                            if (ImGui::Selectable(d.name.c_str(), dm.instance_id==d.instance_id)) {
                                dm.device_guid=DeviceManager::GetDeviceGUIDString(d); dm.instance_id=d.instance_id; rc=true;
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::TableSetColumnIndex(1);

                    std::string btnLabel = "None";
                    if (dm.button_index != -1) btnLabel = "Button " + std::to_string(dm.button_index);
                    else if (dm.hat_index != -1) {
                        btnLabel = "Hat " + std::to_string(dm.hat_index);
                        if (dm.hat_mask & SDL_HAT_UP) btnLabel += " Up";
                        if (dm.hat_mask & SDL_HAT_DOWN) btnLabel += " Down";
                        if (dm.hat_mask & SDL_HAT_LEFT) btnLabel += " Left";
                        if (dm.hat_mask & SDL_HAT_RIGHT) btnLabel += " Right";
                    }
                    ImGui::Text("%s", btnLabel.c_str());

                    ImGui::TableSetColumnIndex(2);
                    std::string flabel = dm.target_field_id.empty() ? "None" : dm.target_field_id;
                    for (auto& [pf2,fd2] : digitalFields) if (pf2->fieldId==dm.target_field_id) { flabel=fd2->label; break; }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##df", flabel.c_str())) {
                        if (ImGui::Selectable("None", dm.target_field_id.empty())) { dm.target_field_id=""; rc=true; }
                        for (auto& [pf2,fd2] : digitalFields) {
                            bool s = dm.target_field_id==pf2->fieldId;
                            if (ImGui::Selectable(fd2->label.c_str(), s)) { dm.target_field_id=pf2->fieldId; rc=true; }
                            if (s) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TableSetColumnIndex(3);
                    const char* modes[] = { "Momentary", "Toggle", "Set On", "Set Off" };
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##mode", (int*)&dm.mode, modes, IM_ARRAYSIZE(modes))) {
                        changed = true;
                    }

                    ImGui::TableSetColumnIndex(4);
                    bool isListening = m_ListeningState.active && m_ListeningState.type == ListeningState::Digital && m_ListeningState.targetName == "digital" && m_ListeningState.listIndex == i;
                    if (isListening) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                        if (ImGui::Button("Waiting...")) CancelListening();
                        ImGui::PopStyleColor();
                        ImGui::SetItemTooltip("Waiting for input... Click to cancel.");
                    } else {
                        if (ImGui::Button("Bind")) StartListening(ListeningState::Digital, "digital", i);
                        ImGui::SetItemTooltip("Click to bind a button.");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete")) toDelete = i;
                    if (rc) changed = true;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (toDelete != -1) { profile.digitalMappings.erase(profile.digitalMappings.begin()+toDelete); changed=true; }
        }
    }

    // ── Button → Analog mappings ──────────────────────────────────────────────
    ImGui::Spacing(); ImGui::Separator();
    ImGui::Text("Button to Analog Mappings");
    ImGui::TextWrapped("Override an analog output with a fixed value when a button is held.");
    if (ImGui::Button("Add Mapping")) { profile.buttonMappings.push_back({}); changed = true; }

    int bToDelete = -1;
    if (ImGui::BeginTable("t_btn_analog", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Button",   ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn("Target",   ImGuiTableColumnFlags_WidthFixed, 150.f);
        ImGui::TableSetupColumn("On Val",   ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Off Val",  ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)profile.buttonMappings.size(); ++i) {
            auto& bm = profile.buttonMappings[i];
            bool rc = false;
            ImGui::PushID(1000+i); ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            std::string dname = "None";
            for (const auto& d : m_DeviceManager.GetDevices()) if (d.instance_id==bm.instance_id) { dname=d.name; break; }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##bd", dname.c_str())) {
                if (ImGui::Selectable("None", bm.instance_id==0)) { bm.device_guid=""; bm.instance_id=0; rc=true; }
                for (const auto& d : m_DeviceManager.GetDevices())
                    if (ImGui::Selectable(d.name.c_str(), bm.instance_id==d.instance_id)) {
                        bm.device_guid=DeviceManager::GetDeviceGUIDString(d); bm.instance_id=d.instance_id; rc=true;
                    }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##bb", &bm.button_index)) rc=true;

            ImGui::TableSetColumnIndex(2);
            std::string tlabel = bm.target_output_name.empty() ? "None" : bm.target_output_name;
            if (outDef) for (auto& [pf2,fd2] : GetEnabledFields(*outDef, FieldType::AnalogAxis))
                if (pf2->fieldId==bm.target_output_name) { tlabel=fd2->label; break; }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##bt", tlabel.c_str())) {
                if (ImGui::Selectable("None", bm.target_output_name.empty())) { bm.target_output_name=""; rc=true; }
                if (outDef) {
                    for (auto& [pf2,fd2] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
                        bool s = bm.target_output_name==pf2->fieldId;
                        if (ImGui::Selectable(fd2->label.c_str(), s)) { bm.target_output_name=pf2->fieldId; rc=true; }
                    }
                } else {
                    for (const auto& name : m_GenericOutputs)
                        if (ImGui::Selectable(name.c_str(), bm.target_output_name==name)) { bm.target_output_name=name; rc=true; }
                }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##bon", &bm.on_value, 0.01f, -1.f, 1.f)) rc=true;
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##boff",&bm.off_value,0.01f, -1.f, 1.f)) rc=true;
            ImGui::TableSetColumnIndex(5);
            bool isListening = m_ListeningState.active && m_ListeningState.type == ListeningState::Digital && m_ListeningState.targetName == "button_analog" && m_ListeningState.listIndex == i;
            if (isListening) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                if (ImGui::Button("Waiting...")) CancelListening();
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("Waiting for input... Click to cancel.");
            } else {
                if (ImGui::Button("Bind")) StartListening(ListeningState::Digital, "button_analog", i);
                ImGui::SetItemTooltip("Click to bind a button.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) bToDelete=i;
            if (rc) changed=true;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (bToDelete != -1) { profile.buttonMappings.erase(profile.buttonMappings.begin()+bToDelete); changed=true; }

    // Protocol Selection
    ImGui::Spacing(); ImGui::Separator();

    if (changed) SaveProfile(profile);

    ImGui::Separator();
    ImGui::Text("Output Preview:");
    ImGui::TextWrapped("%s", GetOutputPreview().c_str());
}

float InputMapper::ProcessAxis(const InputSource &cfg) {
    if (cfg.axisIndex < 0 || cfg.instance_id == 0) return 0.f;
    SDL_Joystick *j = GetJoystickByID(cfg.instance_id, m_DeviceManager);
    if (!j) return 0.f;
    Sint16 raw = SDL_GetJoystickAxis(j, cfg.axisIndex);
    float norm = raw < 0 ? (float)raw/32768.f : (float)raw/32767.f;
    if (cfg.invert) norm = -norm;
    if (std::abs(norm) < cfg.deadzone) norm = 0.f;
    else norm = norm>0 ? (norm-cfg.deadzone)/(1.f-cfg.deadzone) : (norm+cfg.deadzone)/(1.f-cfg.deadzone);
    float r = std::clamp(norm, -1.f, 1.f);
    if (cfg.outputRange==1) r=(r+1.f)*0.5f;
    else if (cfg.outputRange==2) r=(r-1.f)*0.5f;
    return r;
}

bool InputMapper::Update(bool dynamic_rate) {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) return false;
    auto &profile = m_Profiles[m_SelectedProfileIndex];
    const ProtocolDefinition* outDef = GetActiveOutputDefinition();

    // Collect analog values
    std::map<std::string,float> analogValues;
    if (outDef) {
        for (auto& [pf,fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
            auto it = profile.outputToInput.find(pf->fieldId);
            analogValues[pf->fieldId] = it!=profile.outputToInput.end() ? ProcessAxis(it->second) : 0.f;
        }
        for (const auto& bm : profile.buttonMappings) {
            if (bm.instance_id==0 || bm.target_output_name.empty()) continue;
            SDL_Joystick* j = GetJoystickByID(bm.instance_id, m_DeviceManager);
            if (j && SDL_GetJoystickButton(j, bm.button_index)) analogValues[bm.target_output_name]=bm.on_value;
        }
    } else {
        for (const auto& name : m_GenericOutputs) analogValues[name]=0.f;
        for (const auto& [k,src] : profile.outputToInput) analogValues[k]=ProcessAxis(src);
        for (const auto& bm : profile.buttonMappings) {
            if (bm.instance_id==0||bm.target_output_name.empty()) continue;
            SDL_Joystick* j = GetJoystickByID(bm.instance_id, m_DeviceManager);
            if (j && SDL_GetJoystickButton(j, bm.button_index)) analogValues[bm.target_output_name]=bm.on_value;
        }
    }

    // Collect digital values
    std::map<std::string,bool> digitalValues;
    if (outDef) {
        // 1. Update states from all buttons and update last_physical_state
        for (auto& dm : profile.digitalMappings) {
            if (dm.instance_id==0||dm.target_field_id.empty()) continue;
            SDL_Joystick* j = GetJoystickByID(dm.instance_id, m_DeviceManager);
            if (!j) {
                dm.last_physical_state = false; // Device disconnected
                continue;
            }

            bool pressed = false;
            if (dm.button_index != -1) pressed = SDL_GetJoystickButton(j, dm.button_index);
            else if (dm.hat_index != -1) pressed = (SDL_GetJoystickHat(j, dm.hat_index) & dm.hat_mask);

            if (dm.mode != ButtonToDigitalMapping::Mode::Momentary) {
                if (pressed && !dm.last_physical_state) { // on rising edge
                    auto it = profile.digitalToggleStates.find(dm.target_field_id);
                    bool currentState = (it != profile.digitalToggleStates.end()) ? it->second : false;

                    switch (dm.mode) {
                        case ButtonToDigitalMapping::Mode::Toggle:
                            profile.digitalToggleStates[dm.target_field_id] = !currentState;
                            break;
                        case ButtonToDigitalMapping::Mode::SetOn:
                            profile.digitalToggleStates[dm.target_field_id] = true;
                            break;
                        case ButtonToDigitalMapping::Mode::SetOff:
                            profile.digitalToggleStates[dm.target_field_id] = false;
                            break;
                        default: break;
                    }
                }
            }
            dm.last_physical_state = pressed;
        }

        // 2. Determine final value for each field
        for (auto& [pf,fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
            auto it = profile.digitalToggleStates.find(pf->fieldId);
            bool value = (it != profile.digitalToggleStates.end()) ? it->second : false;
            for (const auto& dm : profile.digitalMappings) {
                if (dm.target_field_id != pf->fieldId || dm.mode != ButtonToDigitalMapping::Mode::Momentary) continue;
                if (dm.last_physical_state) {
                    value = true;
                    break;
                }
            }
            digitalValues[pf->fieldId] = value;
        }
    }

    // Change detection snapshot
    std::map<std::string,float> snapshot;
    for (const auto& [k,v] : analogValues)  snapshot[k]=v;
    for (const auto& [k,v] : digitalValues)  snapshot["__d_"+k]=v?1.f:0.f;

    bool should_send = false;
    if (dynamic_rate) {
        Uint64 now = SDL_GetTicks();
        should_send = (snapshot!=m_LastOutputValues) || (now-m_LastBroadcastTime)>=500;
    } else {
        should_send = true;
    }
    if (!should_send) return false;

    auto& osc = OSCServer::GetInstance();
#ifdef ENABLE_WEBSOCKETS
    auto& ws  = WebSocketServer::GetInstance();
#endif

    if (outDef) {
        const ProtocolDefinition* oscDef = nullptr;
        if (m_SelectedProtocolView == 0) {
            oscDef = outDef;
        } else {
            std::string oscId = osc.GetOutputDefinitionId();
            if (!oscId.empty()) oscDef = ProtocolRegistry::GetInstance().FindById(oscId);
            else {
                std::string protocol = osc.GetProtocol();
                if (protocol == "SteamLink OSC") {
                    static ProtocolDefinition s_steamLinkDef; s_steamLinkDef = OSCSteamLinkProtocol::CreateDefaultDefinition();
                    oscDef = &s_steamLinkDef;
                } else if (protocol == "Project Babble OSC") {
                    static ProtocolDefinition s_projectBabbleDef; s_projectBabbleDef = OSCProjectBabbleProtocol::CreateDefaultDefinition();
                    oscDef = &s_projectBabbleDef;
                }
            }
        }
#ifdef ENABLE_WEBSOCKETS
        const auto* wsDef  = ProtocolRegistry::GetInstance().FindById(ws.GetOutputDefinitionId());
#else
        const ProtocolDefinition* wsDef = nullptr;
#endif

        for (auto& [pf,fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
            // Only send if this field actually has a device input assigned.
            bool hasSrc = false;
            {
                auto it = profile.outputToInput.find(pf->fieldId);
                if (it != profile.outputToInput.end() && it->second.instance_id != 0) hasSrc = true;
            }
            if (!hasSrc) {
                for (const auto& bm : profile.buttonMappings)
                    if (bm.instance_id != 0 && bm.target_output_name == pf->fieldId) { hasSrc = true; break; }
            }
            if (!hasSrc) continue;

            float val = analogValues.count(pf->fieldId) ? analogValues[pf->fieldId] : 0.f;
            if (osc.IsRunning() && oscDef)
                for (const auto& op : oscDef->fields)
                    if (op.fieldId==pf->fieldId && op.enabled) { osc.Send(op.oscPath.c_str(),"f",val); break; }
#ifdef ENABLE_WEBSOCKETS
            if (ws.IsRunning() && wsDef)
                for (const auto& wp : wsDef->fields)
                    if (wp.fieldId==pf->fieldId && wp.enabled) { ws.Broadcast(wp.wsKey, val); break; }
#endif
        }
        for (auto& [pf,fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
            // Only send if this field has a button mapped to it.
            bool hasSrc = false;
            for (const auto& dm : profile.digitalMappings)
                if (dm.instance_id != 0 && dm.target_field_id == pf->fieldId) { hasSrc = true; break; }
            if (!hasSrc) continue;

            int val = digitalValues.count(pf->fieldId) && digitalValues[pf->fieldId] ? 1 : 0;
            if (osc.IsRunning() && oscDef)
                for (const auto& op : oscDef->fields)
                    if (op.fieldId==pf->fieldId && op.enabled) { osc.Send(op.oscPath.c_str(), val ? "T" : "F"); break; }
#ifdef ENABLE_WEBSOCKETS
            if (ws.IsRunning() && wsDef)
                for (const auto& wp : wsDef->fields)
                    if (wp.fieldId==pf->fieldId && wp.enabled) { ws.Broadcast(wp.wsKey, val); break; }
#endif
        }
    } else {
        float s=analogValues.count("Steering")?analogValues["Steering"]:0.f;
        float t=analogValues.count("Throttle")?analogValues["Throttle"]:0.f;
        float b=analogValues.count("Brake")?analogValues["Brake"]:0.f;
        float pi=analogValues.count("Pitch")?analogValues["Pitch"]:0.f;
        float ro=analogValues.count("Roll")?analogValues["Roll"]:0.f;
#ifdef ENABLE_WEBSOCKETS
        if (ws.IsRunning())  ws.Broadcast_wheel(s,b,t,pi,ro);
#endif
        if (osc.IsRunning()) osc.SendWheel(s,b,t,pi,ro);
    }

    m_LastOutputValues  = snapshot;
    m_LastBroadcastTime = SDL_GetTicks();
    return true;
}

std::string InputMapper::GetOutputPreview() {
    if (m_SelectedProfileIndex<0||m_SelectedProfileIndex>=(int)m_Profiles.size())
        return "No active profile selected.";
    const auto &profile = m_Profiles[m_SelectedProfileIndex];
    const ProtocolDefinition* outDef = GetActiveOutputDefinition();

    std::stringstream ss;

    // 1. Calculate current values
    std::map<std::string, float> analogValues;
    std::map<std::string, bool> digitalValues;

    if (outDef) {
        for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
            auto it = profile.outputToInput.find(pf->fieldId);
            analogValues[pf->fieldId] = it != profile.outputToInput.end() ? ProcessAxis(it->second) : 0.f;
        }
        for (const auto& bm : profile.buttonMappings) {
            if (bm.instance_id == 0 || bm.target_output_name.empty()) continue; // NOLINT(readability-misleading-indentation)
            SDL_Joystick* j = GetJoystickByID(bm.instance_id, m_DeviceManager);
            if (j && SDL_GetJoystickButton(j, bm.button_index)) analogValues[bm.target_output_name] = bm.on_value;
        }

        // Determine final value for each field for preview
        for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
            auto it = profile.digitalToggleStates.find(pf->fieldId);
            bool value = (it != profile.digitalToggleStates.end()) ? it->second : false;
            for (const auto& dm : profile.digitalMappings) {
                if (dm.target_field_id != pf->fieldId || dm.mode != ButtonToDigitalMapping::Mode::Momentary) continue;
                if (dm.instance_id == 0) continue;
                SDL_Joystick* j = GetJoystickByID(dm.instance_id, m_DeviceManager);
                if (j) {
                    bool pressed = (dm.button_index != -1) ? SDL_GetJoystickButton(j, dm.button_index) : (SDL_GetJoystickHat(j, dm.hat_index) & dm.hat_mask);
                    if (pressed) {
                        value = true;
                        break;
                    }
                }
            }
            digitalValues[pf->fieldId] = value;
        }
    } else {
        for (const auto& name : m_GenericOutputs) analogValues[name] = 0.f;
        for (const auto& [k, src] : profile.outputToInput) analogValues[k] = ProcessAxis(src);
        for (const auto& bm : profile.buttonMappings) {
            if (bm.instance_id == 0 || bm.target_output_name.empty()) continue;
            SDL_Joystick* j = GetJoystickByID(bm.instance_id, m_DeviceManager);
            if (j && SDL_GetJoystickButton(j, bm.button_index)) analogValues[bm.target_output_name] = bm.on_value;
        }
    }

    // 2. Generate Preview String
    auto& osc = OSCServer::GetInstance();
#ifdef ENABLE_WEBSOCKETS
    auto& ws = WebSocketServer::GetInstance();
#endif

    if (outDef) {
        ss << "Active Definition: " << outDef->name << "\n\n";

        if (m_SelectedProtocolView == 0) { // OSC
            // Mirror Update()'s oscDef resolution so the preview only shows
            // fields that the OSC server will actually transmit.
            const ProtocolDefinition* oscDef = outDef;
            {
                std::string oscId = osc.GetOutputDefinitionId();
                if (!oscId.empty()) {
                    const ProtocolDefinition* found = ProtocolRegistry::GetInstance().FindById(oscId);
                    if (found) oscDef = found;
                } else {
                    std::string protocol = osc.GetProtocol();
                    if (protocol == "SteamLink OSC") {
                        static ProtocolDefinition s_slDef;
                        s_slDef = OSCSteamLinkProtocol::CreateDefaultDefinition();
                        oscDef = &s_slDef;
                    } else if (protocol == "Project Babble OSC") {
                        static ProtocolDefinition s_pbDef;
                        s_pbDef = OSCProjectBabbleProtocol::CreateDefaultDefinition();
                        oscDef = &s_pbDef;
                    }
                }
            }

            ss << "[OSC Output]";
            if (!osc.IsRunning()) ss << " (Stopped)";
            else if (oscDef != outDef) ss << " (Separate OSC Definition: " << oscDef->name << ")";
            ss << "\n";

            int sentCount = 0;
            for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
                // Check device mapping
                bool hasSrc = false;
                {
                    auto it = profile.outputToInput.find(pf->fieldId);
                    if (it != profile.outputToInput.end() && it->second.instance_id != 0) hasSrc = true;
                }
                if (!hasSrc)
                    for (const auto& bm : profile.buttonMappings)
                        if (bm.instance_id != 0 && bm.target_output_name == pf->fieldId) { hasSrc = true; break; }
                if (!hasSrc) continue;

                const ProtocolField* op = nullptr;
                for (const auto& f : oscDef->fields)
                    if (f.fieldId == pf->fieldId && f.enabled) { op = &f; break; }
                if (!op) continue;
                ss << "  " << op->oscPath << " " << std::fixed << std::setprecision(4) << analogValues[pf->fieldId] << "\n";
                ++sentCount;
            }
            for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
                // Check device mapping
                bool hasSrc = false;
                for (const auto& dm : profile.digitalMappings)
                    if (dm.instance_id != 0 && dm.target_field_id == pf->fieldId) { hasSrc = true; break; }
                if (!hasSrc) continue;

                const ProtocolField* op = nullptr;
                for (const auto& f : oscDef->fields)
                    if (f.fieldId == pf->fieldId && f.enabled) { op = &f; break; }
                if (!op) continue;
                ss << "  " << op->oscPath << " " << (digitalValues[pf->fieldId] ? "T" : "F") << "\n";
                ++sentCount;
            }
            if (sentCount == 0)
                ss << "  (No mapped fields — assign device inputs on this page)\n";
        } else { // WebSocket
#ifdef ENABLE_WEBSOCKETS
            // Mirror Update()'s wsDef resolution.
            const ProtocolDefinition* wsDef = nullptr;
            {
                std::string wsId = ws.GetOutputDefinitionId();
                if (!wsId.empty()) wsDef = ProtocolRegistry::GetInstance().FindById(wsId);
            }

            ss << "[WebSocket Output]";
            if (!ws.IsRunning()) ss << " (Stopped)";
            else if (!wsDef) ss << " (No WS Definition Selected)";
            ss << "\n";

            std::string protoName = ws.GetProtocol();
            auto protocol = ProtocolManager::GetInstance().GetProtocol(protoName);
            if (protocol && wsDef) {
                int sentCount = 0;
                for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
                    bool hasSrc = false;
                    {
                        auto it = profile.outputToInput.find(pf->fieldId);
                        if (it != profile.outputToInput.end() && it->second.instance_id != 0) hasSrc = true;
                    }
                    if (!hasSrc)
                        for (const auto& bm : profile.buttonMappings)
                            if (bm.instance_id != 0 && bm.target_output_name == pf->fieldId) { hasSrc = true; break; }
                    if (!hasSrc) continue;

                    const ProtocolField* wp = nullptr;
                    for (const auto& f : wsDef->fields)
                        if (f.fieldId == pf->fieldId && f.enabled) { wp = &f; break; }
                    if (!wp) continue;
                    ss << "  " << protocol->format(wp->wsKey, analogValues[pf->fieldId]) << "\n";
                    ++sentCount;
                }
                for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
                    bool hasSrc = false;
                    for (const auto& dm : profile.digitalMappings)
                        if (dm.instance_id != 0 && dm.target_field_id == pf->fieldId) { hasSrc = true; break; }
                    if (!hasSrc) continue;

                    const ProtocolField* wp = nullptr;
                    for (const auto& f : wsDef->fields)
                        if (f.fieldId == pf->fieldId && f.enabled) { wp = &f; break; }
                    if (!wp) continue;
                    ss << "  " << protocol->format(wp->wsKey, digitalValues[pf->fieldId] ? 1 : 0) << "\n";
                    ++sentCount;
                }
                if (sentCount == 0)
                    ss << "  (No mapped fields — assign device inputs on this page)\n";
            } else if (!protocol) {
                ss << "  (Unknown Protocol)\n";
            } else {
                ss << "  (No WS definition — select one in the Network tab)\n";
            }
#else
            ss << "WebSockets disabled.\n";
#endif
        }
    } else {
        // Legacy
        ss << "Legacy Mode (No Output Definition Selected)\n\n";

        float s = analogValues["Steering"];
        float t = analogValues["Throttle"];
        float b = analogValues["Brake"];
        float pi = analogValues["Pitch"];
        float ro = analogValues["Roll"];

        std::map<std::string, float> wheelValues;
        if (analogValues.count("Steering")) wheelValues["wheel"] = s;
        if (analogValues.count("Throttle")) wheelValues["throttle"] = t;
        if (analogValues.count("Brake")) wheelValues["brake"] = b;
        if (analogValues.count("Pitch")) wheelValues["pitch"] = pi;
        if (analogValues.count("Roll")) wheelValues["roll"] = ro;

        if (m_SelectedProtocolView == 0) {
            ss << "[OSC Output]";
            if (!osc.IsRunning()) ss << " (Stopped)";
            ss << "\n";
            ss << "  /wheel/steer " << std::fixed << std::setprecision(4) << s << "\n";
            ss << "  /wheel/throttle " << t << "\n";
            ss << "  /wheel/brake " << b << "\n";
            ss << "  /wheel/pitch " << pi << "\n";
            ss << "  /wheel/roll " << ro << "\n";
            ss << "  (Buttons not previewed in legacy mode)\n\n";
        } else {
#ifdef ENABLE_WEBSOCKETS
            ss << "[WebSocket Output]";
            if (!ws.IsRunning()) ss << " (Stopped)";
            ss << "\n";
            std::string protoName = ws.GetProtocol();
            auto protocol = ProtocolManager::GetInstance().GetProtocol(protoName);
            if (protocol) {
                ss << "  " << protocol->format_wheel(wheelValues) << "\n";
            } else {
                ss << "  (Unknown Protocol)\n";
            }
#else
            ss << "WebSockets disabled.\n";
#endif
        }
    }

    return ss.str();
}

void InputMapper::LoadProfiles() {
    m_Profiles.clear();
    try {
        auto dir = GetMappingsDirectory();
        if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
        for (const auto &e : std::filesystem::directory_iterator(dir)) {
            if (!e.is_regular_file()||e.path().extension()!=".json") continue;
            std::ifstream f(e.path()); if (!f) continue;
            try {
                json data=json::parse(f);
                if (!data.contains("name")||!data["name"].is_string()||data["name"].get<std::string>().empty()) continue;
                MappingProfile p; p.name=data["name"];
                if (data.contains("mappings"))
                    for (auto& [key,val] : data["mappings"].items()) {
                        InputSource src;
                        src.deviceGuid=val.value("device_guid",""); src.axisIndex=val.value("axis",-1);
                        src.invert=val.value("invert",false); src.deadzone=val.value("deadzone",0.05f);
                        src.outputRange=val.value("range",0);
                        p.outputToInput[key]=src;
                    }
                if (data.contains("haptic_targets"))
                    for (const auto& item : data["haptic_targets"]) {
                        HapticTarget t;
                        t.virtual_id=item.value("virtual_id",0); t.name=item.value("name","Target");
                        t.device_guid=item.value("device_guid","");
                        t.enable_rumble=item.value("enable_rumble",true); t.enable_constant=item.value("enable_constant",true);
                        t.enable_periodic=item.value("enable_periodic",true); t.enable_condition=item.value("enable_condition",true);
                        p.hapticTargets.push_back(t);
                    }
                if (data.contains("button_mappings"))
                    for (const auto& item : data["button_mappings"]) {
                        ButtonToAnalogMapping bm;
                        bm.device_guid=item.value("device_guid",""); bm.button_index=item.value("button_index",0);
                        bm.target_output_name=item.value("target_output_name","");
                        bm.on_value=item.value("on_value",1.f); bm.off_value=item.value("off_value",0.f);
                        p.buttonMappings.push_back(bm);
                    }
                if (data.contains("digital_mappings"))
                    for (const auto& item : data["digital_mappings"]) {
                        ButtonToDigitalMapping dm;
                        dm.device_guid=item.value("device_guid",""); dm.button_index=item.value("button_index",-1);
                        dm.hat_index=item.value("hat_index",-1);
                        dm.hat_mask=item.value("hat_mask",0);
                        dm.target_field_id=item.value("target_field_id","");
                        if (item.contains("mode")) {
                            dm.mode = item.value("mode", ButtonToDigitalMapping::Mode::Momentary);
                        } else if (item.value("is_toggle", false)) {
                            dm.mode = ButtonToDigitalMapping::Mode::Toggle;
                        }
                        p.digitalMappings.push_back(dm);
                    }
                if (data.contains("digital_toggle_states")) {
                    for (const auto& [key, val] : data["digital_toggle_states"].items()) {
                        if (val.is_boolean()) p.digitalToggleStates[key] = val.get<bool>();
                    }
                }

                p.oscOutputProtocolId = data.value("osc_output_protocol_id", "");
                p.oscInputProtocolId = data.value("osc_input_protocol_id", "");
                p.wsOutputProtocolId = data.value("ws_output_protocol_id", "");
                p.wsInputProtocolId = data.value("ws_input_protocol_id", "");
                p.selectedProtocolView = data.value("selected_protocol_view", 0);

                p.oscSendHost      = data.value("osc_send_host",       "127.0.0.1");
                p.oscSendPort      = data.value("osc_send_port",       9066);
                p.oscRecvPort      = data.value("osc_recv_port",       9068);
                p.oscOutputEnabled = data.value("osc_output_enabled",  true);
                p.oscInputEnabled  = data.value("osc_input_enabled",   true);

                p.wsPort          = data.value("ws_port",           4269);
                p.wsOutputEnabled = data.value("ws_output_enabled", true);
                p.wsInputEnabled  = data.value("ws_input_enabled",  true);

                m_Profiles.push_back(p);
            } catch (const std::exception& ex) { SDL_Log("Failed to parse profile %s: %s",e.path().string().c_str(),ex.what()); }
        }
    } catch (const std::exception& ex) { SDL_Log("Failed to load profiles: %s",ex.what()); }
}

void InputMapper::SaveProfile(const MappingProfile &profile) const {
    try {
        auto dir=GetMappingsDirectory();
        if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
        auto path=dir/(profile.name+".json");
        json data; data["name"]=profile.name; data["mappings"]=json::object();
        for (const auto& [k,v] : profile.outputToInput)
            if (v.axisIndex!=-1)
                data["mappings"][k]={{"device_guid",v.deviceGuid},{"axis",v.axisIndex},
                                     {"invert",v.invert},{"deadzone",v.deadzone},{"range",v.outputRange}};
        data["haptic_targets"]=json::array();
        for (const auto& t : profile.hapticTargets)
            data["haptic_targets"].push_back({{"virtual_id",t.virtual_id},{"name",t.name},{"device_guid",t.device_guid},
                {"enable_rumble",t.enable_rumble},{"enable_constant",t.enable_constant},
                {"enable_periodic",t.enable_periodic},{"enable_condition",t.enable_condition}});
        data["button_mappings"]=json::array();
        for (const auto& bm : profile.buttonMappings)
            data["button_mappings"].push_back({{"device_guid",bm.device_guid},{"button_index",bm.button_index},
                {"target_output_name",bm.target_output_name},{"on_value",bm.on_value},{"off_value",bm.off_value}});
        data["digital_mappings"]=json::array();
        for (const auto& dm : profile.digitalMappings) {
            json j = {{"device_guid",dm.device_guid}, {"target_field_id",dm.target_field_id}, {"mode", dm.mode}};
            if (dm.hat_index != -1) { j["hat_index"]=dm.hat_index; j["hat_mask"]=dm.hat_mask; }
            else { j["button_index"]=dm.button_index; }
            data["digital_mappings"].push_back(j);
        }
        data["digital_toggle_states"] = profile.digitalToggleStates;

        data["osc_output_protocol_id"] = profile.oscOutputProtocolId;
        data["osc_input_protocol_id"] = profile.oscInputProtocolId;
        data["ws_output_protocol_id"] = profile.wsOutputProtocolId;
        data["ws_input_protocol_id"] = profile.wsInputProtocolId;
        data["selected_protocol_view"] = profile.selectedProtocolView;

        data["osc_send_host"]      = profile.oscSendHost;
        data["osc_send_port"]      = profile.oscSendPort;
        data["osc_recv_port"]      = profile.oscRecvPort;
        data["osc_output_enabled"] = profile.oscOutputEnabled;
        data["osc_input_enabled"]  = profile.oscInputEnabled;

        data["ws_port"]           = profile.wsPort;
        data["ws_output_enabled"] = profile.wsOutputEnabled;
        data["ws_input_enabled"]  = profile.wsInputEnabled;

        std::ofstream o(path); if (o) o<<data.dump(4);
    } catch (const std::exception& ex) { SDL_Log("Failed to save profile: %s",ex.what()); }
}

std::vector<HapticTarget>* InputMapper::GetCurrentHapticTargets() {
    if (m_SelectedProfileIndex>=0&&m_SelectedProfileIndex<(int)m_Profiles.size())
        return &m_Profiles[m_SelectedProfileIndex].hapticTargets;
    return nullptr;
}

void InputMapper::HandleDeviceConnectionChange() {
    std::map<std::string,SDL_JoystickID> guidMap;
    for (const auto& d : m_DeviceManager.GetDevices()) guidMap[DeviceManager::GetDeviceGUIDString(d)]=d.instance_id;
    for (auto& p : m_Profiles) {
        for (auto& [k,src] : p.outputToInput) { auto it=guidMap.find(src.deviceGuid); src.instance_id=it!=guidMap.end()?it->second:0; }
        for (auto& t : p.hapticTargets)        { if(t.device_guid.empty())continue; auto it=guidMap.find(t.device_guid); t.instance_id=it!=guidMap.end()?it->second:0; }
        for (auto& bm : p.buttonMappings)      { if(bm.device_guid.empty())continue; auto it=guidMap.find(bm.device_guid); bm.instance_id=it!=guidMap.end()?it->second:0; }
        for (auto& dm : p.digitalMappings)     { if(dm.device_guid.empty())continue; auto it=guidMap.find(dm.device_guid); dm.instance_id=it!=guidMap.end()?it->second:0; }
    }
}

void InputMapper::UpdateActiveProtocols() {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) {
        ProtocolManager::GetInstance().SetActiveInputProtocolId("");
        return;
    }

    const auto& p = m_Profiles[m_SelectedProfileIndex];
    ProtocolManager::GetInstance().SetActiveInputProtocolId(p.oscInputProtocolId);

    // ── OSC server ────────────────────────────────────────────────────────────
    auto& osc = OSCServer::GetInstance();

    // Apply definition IDs (output + input protocol selections).
    if (!p.oscOutputProtocolId.empty())
        osc.SetOutputDefinition(p.oscOutputProtocolId);
    else
        osc.SetOutputDefinition("");

    if (!p.oscInputProtocolId.empty())
        osc.SetInputDefinition(p.oscInputProtocolId);
    else
        osc.SetInputDefinition("");

    // Apply enable flags and port settings.  Ports only take effect after a
    // restart, so we write them to the UI fields without restarting.
    osc.SetOutputEnabled(p.oscOutputEnabled);
    osc.SetInputEnabled(p.oscInputEnabled);
    osc.SetPortsFromProfile(p.oscSendHost, p.oscSendPort, p.oscRecvPort);

#ifdef ENABLE_WEBSOCKETS
    // ── WebSocket server ──────────────────────────────────────────────────────
    auto& ws = WebSocketServer::GetInstance();

    if (!p.wsOutputProtocolId.empty())
        ws.SetOutputDefinition(p.wsOutputProtocolId);
    else
        ws.SetOutputDefinition("");

    if (!p.wsInputProtocolId.empty())
        ws.SetInputDefinition(p.wsInputProtocolId);
    else
        ws.SetInputDefinition("");

    ws.SetOutputEnabled(p.wsOutputEnabled);
    ws.SetInputEnabled(p.wsInputEnabled);
    ws.SetPortFromProfile(p.wsPort);
#endif
}

void InputMapper::SnapshotServerSettings(MappingProfile& profile) const {
    auto& osc = OSCServer::GetInstance();
    profile.oscOutputProtocolId = osc.GetOutputDefinitionId();
    profile.oscInputProtocolId  = osc.GetInputDefinitionId();
    profile.oscSendHost         = osc.GetSendHost();
    profile.oscSendPort         = osc.GetSendPort();
    profile.oscRecvPort         = osc.GetReceivePort();
    profile.oscOutputEnabled    = osc.IsOutputEnabled();
    profile.oscInputEnabled     = osc.IsInputEnabled();

#ifdef ENABLE_WEBSOCKETS
    auto& ws = WebSocketServer::GetInstance();
    profile.wsOutputProtocolId = ws.GetOutputDefinitionId();
    profile.wsInputProtocolId  = ws.GetInputDefinitionId();
    profile.wsPort             = ws.GetPort();
    profile.wsOutputEnabled    = ws.IsOutputEnabled();
    profile.wsInputEnabled     = ws.IsInputEnabled();
#endif
}

bool InputMapper::IsOutputAddressBound(const std::string& address) const {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) {
        return false; // No profile, so nothing is bound.
    }
    const auto& profile = m_Profiles[m_SelectedProfileIndex];

    std::string fieldId;

    // 1. Check active protocol definitions from the profile
    const ProtocolDefinition* oscDef = !profile.oscOutputProtocolId.empty() ? ProtocolRegistry::GetInstance().FindById(profile.oscOutputProtocolId) : nullptr;
    if (oscDef) {
        for (const auto& field : oscDef->fields) {
            if (field.oscPath == address) {
                fieldId = field.fieldId;
                break;
            }
        }
    }

    if (fieldId.empty()) {
        const ProtocolDefinition* wsDef = !profile.wsOutputProtocolId.empty() ? ProtocolRegistry::GetInstance().FindById(profile.wsOutputProtocolId) : nullptr;
        if (wsDef) {
            for (const auto& field : wsDef->fields) {
                if (field.wsKey == address) {
                    fieldId = field.fieldId;
                    break;
                }
            }
        }
    }

    // 2. If not found, check legacy protocols
    if (fieldId.empty()) {
        // This mapping is for legacy protocols that don't use definitions.
        if (address == "/wheel/steer" || address == "wheel") fieldId = "Steering";
        else if (address == "/wheel/throttle" || address == "throttle") fieldId = "Throttle";
        else if (address == "/wheel/brake" || address == "brake") fieldId = "Brake";
        else if (address == "/wheel/clutch" || address == "clutch") fieldId = "Clutch";
        else if (address == "/wheel/handbrake" || address == "handbrake") fieldId = "Handbrake";
        else if (address == "/wheel/pitch" || address == "pitch") fieldId = "Pitch";
        else if (address == "/wheel/roll" || address == "roll") fieldId = "Roll";
    }

    if (fieldId.empty()) {
        return false;
    }

    // 3. Check if fieldId is bound in the current profile
    if (profile.outputToInput.count(fieldId) && profile.outputToInput.at(fieldId).axisIndex != -1) {
        return true;
    }
    for (const auto& mapping : profile.buttonMappings) {
        if (mapping.target_output_name == fieldId && mapping.button_index != -1) return true;
    }
    for (const auto& mapping : profile.digitalMappings) {
        if (mapping.target_field_id == fieldId && (mapping.button_index != -1 || mapping.hat_index != -1)) return true;
    }

    return false;
}

#ifdef ENABLE_EXCLUSIVE_INPUT

#endif