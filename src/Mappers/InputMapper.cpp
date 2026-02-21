#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "Mappers/OutputMapper.h"
#include "Network/WebSocketServer.h"
#include "Network/OSCServer.h"
#include "Protocols/ProtocolRegistry.h"
#include "Protocols/ProtocolDefinition.h"
#include "Protocols/ProtocolManager.h"
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

// Returns the active output ProtocolDefinition for whichever server has one selected.
const ProtocolDefinition* GetActiveOutputDefinition() {
    const std::string oscId = OSCServer::GetInstance().GetOutputDefinitionId();
    if (!oscId.empty()) return ProtocolRegistry::GetInstance().FindById(oscId);
#ifdef ENABLE_WEBSOCKETS
    const std::string wsId = WebSocketServer::GetInstance().GetOutputDefinitionId();
    if (!wsId.empty())  return ProtocolRegistry::GetInstance().FindById(wsId);
#endif
    return nullptr;
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

void InputMapper::LoadConfig(PreferencesManager &prefs) {
    LoadProfiles();
    std::string last = prefs.GetString("InputMapper", "LastProfile", "");
    for (int i = 0; i < (int)m_Profiles.size(); ++i)
        if (m_Profiles[i].name == last) { m_SelectedProfileIndex = i; break; }
    if (m_SelectedProfileIndex != -1)
        OutputMapper::GetInstance().SetActiveHapticTargets(&m_Profiles[m_SelectedProfileIndex].hapticTargets);
    HandleDeviceConnectionChange();
}

void InputMapper::SaveConfig(PreferencesManager &prefs) const {
    prefs.SetString("InputMapper", "LastProfile",
        m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < (int)m_Profiles.size()
            ? m_Profiles[m_SelectedProfileIndex].name : "");
}

void InputMapper::SaveCurrentProfile() const {
    if (m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < (int)m_Profiles.size())
        SaveProfile(m_Profiles[m_SelectedProfileIndex]);
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
    } else if (m_ListeningState.type == ListeningState::Button) {
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
        }
    }
}

void InputMapper::DrawContent() {
    HandleDeviceConnectionChange();
    UpdateListening();
    ImGui::Begin("Input Mapper");

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
    if (old_idx != m_SelectedProfileIndex)
        OutputMapper::GetInstance().SetActiveHapticTargets(
            m_SelectedProfileIndex != -1 ? &m_Profiles[m_SelectedProfileIndex].hapticTargets : nullptr);

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
                    m_Profiles.erase(m_Profiles.begin() + m_SelectedProfileIndex);
                    m_SelectedProfileIndex = -1;
                    OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(120,0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
    }

    ImGui::Separator();
    if (m_SelectedProfileIndex == -1) { ImGui::TextDisabled("Select or create a profile above."); ImGui::End(); return; }

    bool changed = false;
    MappingProfile &profile = m_Profiles[m_SelectedProfileIndex];
    ImGui::Text("'%s' Mappings", profile.name.c_str());

    const ProtocolDefinition* outDef = GetActiveOutputDefinition();

    // Axis combo helper
    auto drawAxisCombo = [&](const std::string& id, InputSource& src, const char* comboId, float colW) {
        std::string preview = "None";
        if (src.instance_id != 0)
            for (const auto& d : m_DeviceManager.GetDevices())
                if (d.instance_id == src.instance_id) { preview = d.name + " - Axis " + std::to_string(src.axisIndex); break; }

        float bindW = ImGui::CalcTextSize("Bind").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        bool hasSrc = src.axisIndex != -1;
        if (hasSrc) {
            float sp = ImGui::GetStyle().ItemSpacing.x;
            float xw = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x*2 + sp;
            float iw = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Invert").x + sp;
            float dw = 80.f + sp; float rw = 100.f + sp;
            ImGui::SetNextItemWidth(colW - xw - iw - dw - rw - bindW);
        } else {
            ImGui::SetNextItemWidth(colW - bindW);
        }

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
            if (ImGui::Button("Stop")) CancelListening();
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Bind")) StartListening(ListeningState::Axis, id);
        }
        ImGui::SetItemTooltip("Press to detect axis input");

        if (hasSrc) {
            ImGui::SameLine();
            if (ImGui::Button("X")) { src = {}; changed = true; }
            ImGui::SetItemTooltip("Clear");
            ImGui::SameLine();
            if (ImGui::Checkbox("Invert", &src.invert)) changed = true;
            ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            if (ImGui::SliderFloat("DZ", &src.deadzone, 0.f, 0.5f, "%.3f")) changed = true;
            ImGui::SetItemTooltip("Deadzone");
            ImGui::SameLine(); ImGui::SetNextItemWidth(100);
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
            if (ImGui::BeginTable("t_digital", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Device",        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Button Index",  ImGuiTableColumnFlags_WidthFixed, 90.f);
                ImGui::TableSetupColumn("Digital Field", ImGuiTableColumnFlags_WidthFixed, 170.f);
                ImGui::TableSetupColumn("",              ImGuiTableColumnFlags_WidthFixed, 90.f);
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
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputInt("##db", &dm.button_index)) rc=true;

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
                    bool isListening = m_ListeningState.active && m_ListeningState.type == ListeningState::Button && m_ListeningState.targetName == "digital" && m_ListeningState.listIndex == i;
                    if (isListening) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                        if (ImGui::Button("Stop")) CancelListening();
                        ImGui::PopStyleColor();
                    } else {
                        if (ImGui::Button("Bind")) StartListening(ListeningState::Button, "digital", i);
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
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthFixed, 90.f);
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
            bool isListening = m_ListeningState.active && m_ListeningState.type == ListeningState::Button && m_ListeningState.targetName == "button_analog" && m_ListeningState.listIndex == i;
            if (isListening) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                if (ImGui::Button("Stop")) CancelListening();
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Button("Bind")) StartListening(ListeningState::Button, "button_analog", i);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) bToDelete=i;
            if (rc) changed=true;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (bToDelete != -1) { profile.buttonMappings.erase(profile.buttonMappings.begin()+bToDelete); changed=true; }

#ifdef ENABLE_EXCLUSIVE_INPUT
    ImGui::BeginDisabled(true);
    bool ex = m_ExclusiveModeHandler.IsEnabled();
    ImGui::Checkbox("Exclusive Mode (Hide from other apps)", &ex);
    ImGui::EndDisabled();
#endif

    if (changed) SaveProfile(profile);

    ImGui::Separator();
    ImGui::Text("Output Preview:");
    ImGui::TextWrapped("%s", GetOutputPreview().c_str());
    ImGui::End();
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
    const auto &profile = m_Profiles[m_SelectedProfileIndex];
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
        for (auto& [pf,fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) digitalValues[pf->fieldId]=false;
        for (const auto& dm : profile.digitalMappings) {
            if (dm.instance_id==0||dm.target_field_id.empty()) continue;
            SDL_Joystick* j = GetJoystickByID(dm.instance_id, m_DeviceManager);
            if (j && SDL_GetJoystickButton(j, dm.button_index)) digitalValues[dm.target_field_id]=true;
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
        const auto* oscDef = ProtocolRegistry::GetInstance().FindById(osc.GetOutputDefinitionId());
#ifdef ENABLE_WEBSOCKETS
        const auto* wsDef  = ProtocolRegistry::GetInstance().FindById(ws.GetOutputDefinitionId());
#else
        const ProtocolDefinition* wsDef = nullptr;
#endif

        for (auto& [pf,fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
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
            int val = digitalValues.count(pf->fieldId) && digitalValues[pf->fieldId] ? 1 : 0;
            if (osc.IsRunning() && oscDef)
                for (const auto& op : oscDef->fields)
                    if (op.fieldId==pf->fieldId && op.enabled) { osc.Send(op.oscPath.c_str(),"i",val); break; }
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
            if (bm.instance_id == 0 || bm.target_output_name.empty()) continue;
            SDL_Joystick* j = GetJoystickByID(bm.instance_id, m_DeviceManager);
            if (j && SDL_GetJoystickButton(j, bm.button_index)) analogValues[bm.target_output_name] = bm.on_value;
        }
        
        for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
            digitalValues[pf->fieldId] = false;
        }
        for (const auto& dm : profile.digitalMappings) {
            if (dm.instance_id == 0 || dm.target_field_id.empty()) continue;
            SDL_Joystick* j = GetJoystickByID(dm.instance_id, m_DeviceManager);
            if (j && SDL_GetJoystickButton(j, dm.button_index)) digitalValues[dm.target_field_id] = true;
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

        // OSC
        std::string oscDefId = osc.GetOutputDefinitionId();
        if (osc.IsRunning()) {
            if (!oscDefId.empty() && oscDefId == outDef->id) {
                ss << "[OSC Output]\n";
                for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
                    ss << "  " << pf->oscPath << " " << std::fixed << std::setprecision(4) << analogValues[pf->fieldId] << "\n";
                }
                for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
                    ss << "  " << pf->oscPath << " " << (digitalValues[pf->fieldId] ? 1 : 0) << "\n";
                }
            } else {
                ss << "[OSC Output] (Running, but definition mismatch)\n";
            }
            ss << "\n";
        }

#ifdef ENABLE_WEBSOCKETS
        // WebSocket
        std::string wsDefId = ws.GetOutputDefinitionId();
        if (ws.IsRunning()) {
            if (!wsDefId.empty() && wsDefId == outDef->id) {
                ss << "[WebSocket Output]\n";
                std::string protoName = ws.GetProtocol();
                auto protocol = ProtocolManager::GetInstance().GetProtocol(protoName);
                if (protocol) {
                    for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
                        ss << "  " << protocol->format(pf->wsKey, analogValues[pf->fieldId]) << "\n";
                    }
                    for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::DigitalButton)) {
                        ss << "  " << protocol->format(pf->wsKey, digitalValues[pf->fieldId] ? 1 : 0) << "\n";
                    }
                } else {
                    ss << "  (Unknown Protocol)\n";
                }
            } else {
                ss << "[WebSocket Output] (Running, but definition mismatch)\n";
            }
        }
#endif

    } else {
        // Legacy
        ss << "Legacy Mode (No Output Definition Selected)\n\n";
        
        float s = analogValues["Steering"];
        float t = analogValues["Throttle"];
        float b = analogValues["Brake"];
        float pi = analogValues["Pitch"];
        float ro = analogValues["Roll"];

        if (osc.IsRunning()) {
            ss << "[OSC Output]\n";
            ss << "  /wheel/steer " << std::fixed << std::setprecision(4) << s << "\n";
            ss << "  /wheel/throttle " << t << "\n";
            ss << "  /wheel/brake " << b << "\n";
            ss << "  /wheel/pitch " << pi << "\n";
            ss << "  /wheel/roll " << ro << "\n";
            ss << "  (Buttons not previewed in legacy mode)\n\n";
        }

#ifdef ENABLE_WEBSOCKETS
        if (ws.IsRunning()) {
            ss << "[WebSocket Output]\n";
            std::string protoName = ws.GetProtocol();
            auto protocol = ProtocolManager::GetInstance().GetProtocol(protoName);
            if (protocol) {
                ss << "  " << protocol->format_wheel(s, b, t, pi, ro) << "\n";
            } else {
                ss << "  (Unknown Protocol)\n";
            }
        }
#endif
    }

    if (!osc.IsRunning() 
#ifdef ENABLE_WEBSOCKETS
        && !ws.IsRunning()
#endif
    ) {
        ss << "(No servers running)\n\nRaw Values:\n";
        for(auto const& [key, val] : analogValues) {
            ss << "  " << key << ": " << std::fixed << std::setprecision(4) << val << "\n";
        }
        for(auto const& [key, val] : digitalValues) {
            ss << "  " << key << ": " << (val ? "ON" : "OFF") << "\n";
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
                        dm.device_guid=item.value("device_guid",""); dm.button_index=item.value("button_index",0);
                        dm.target_field_id=item.value("target_field_id","");
                        p.digitalMappings.push_back(dm);
                    }
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
        for (const auto& dm : profile.digitalMappings)
            data["digital_mappings"].push_back({{"device_guid",dm.device_guid},{"button_index",dm.button_index},
                {"target_field_id",dm.target_field_id}});
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

#ifdef ENABLE_EXCLUSIVE_INPUT
void InputMapper::ApplyExclusiveMode() {}
#endif
