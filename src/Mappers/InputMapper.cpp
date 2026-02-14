#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "Mappers/OutputMapper.h"
#include "Network/WebSocketServer.h"
#include "Network/OSCServer.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <memory>
#include <SDL3/SDL_filesystem.h>

using json = nlohmann::json;

std::unique_ptr<InputMapper> InputMapper::s_Instance;

InputMapper &InputMapper::GetInstance()
{
    return *s_Instance;
}

void InputMapper::Init(const DeviceManager &deviceManager)
{
    if (!s_Instance)
        s_Instance.reset(new InputMapper(deviceManager));
}

void InputMapper::Shutdown()
{
    s_Instance.reset();
}

namespace { // Anonymous namespace for private helper functions

SDL_Joystick *GetJoystickByID(SDL_JoystickID id, const DeviceManager &deviceManager) {
    if (id == 0) {
        return nullptr;
    }
    const auto &devices = deviceManager.GetDevices();
    auto it = std::find_if(devices.begin(), devices.end(), [id](const DeviceState &dev) { return dev.instance_id == id; });

    return (it != devices.end()) ? it->joystick : nullptr;
}

std::filesystem::path GetMappingsDirectory() {
    const char *basePath = SDL_GetBasePath();
    std::filesystem::path path;
    if (basePath) {
        path = std::filesystem::path(basePath) / "mappings";
    } else {
        path = "mappings";
    }
    return path;
}

} // namespace

InputMapper::InputMapper(const DeviceManager &deviceManager) : m_DeviceManager(deviceManager) {
}

InputMapper::~InputMapper() {}

void InputMapper::LoadConfig(PreferencesManager &prefs) {
    LoadProfiles();

    std::string lastProfile = prefs.GetString("InputMapper", "LastProfile", "");
    if (!lastProfile.empty()) {
        for (size_t i = 0; i < m_Profiles.size(); ++i) {
            if (m_Profiles[i].name == lastProfile) {
                m_SelectedProfileIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (m_SelectedProfileIndex != -1) {
        OutputMapper::GetInstance().SetActiveHapticTargets(&m_Profiles[m_SelectedProfileIndex].hapticTargets);
    }

    HandleDeviceConnectionChange();
}

void InputMapper::SaveConfig(PreferencesManager &prefs) const {
    if (m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < m_Profiles.size()) {
        prefs.SetString("InputMapper", "LastProfile", m_Profiles[m_SelectedProfileIndex].name);
    } else {
        prefs.SetString("InputMapper", "LastProfile", "");
    }
}

void InputMapper::SaveCurrentProfile() const {
    if (m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < m_Profiles.size()) {
        SaveProfile(m_Profiles[m_SelectedProfileIndex]);
    }
}

void InputMapper::DrawContent() {
    HandleDeviceConnectionChange();

    ImGui::Begin("Input Mapper");

    // --- Profile Management ---
    ImGui::Text("Mapping Profiles");
    ImGui::Separator();

    int old_idx = m_SelectedProfileIndex;
    const char *current_profile_name = (m_SelectedProfileIndex != -1) ? m_Profiles[m_SelectedProfileIndex].name.c_str() : "None";
    if (ImGui::BeginCombo("Active Profile", current_profile_name)) {
        if (ImGui::Selectable("None", m_SelectedProfileIndex == -1)) {
            m_SelectedProfileIndex = -1;
        }
        for (int i = 0; i < m_Profiles.size(); ++i) {
            const bool isSelected = (m_SelectedProfileIndex == i);
            if (ImGui::Selectable(m_Profiles[i].name.c_str(), isSelected)) {
                m_SelectedProfileIndex = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (old_idx != m_SelectedProfileIndex) {
        auto& outputMapper = OutputMapper::GetInstance();
        std::vector<HapticTarget>* old_targets = (old_idx != -1) ? &m_Profiles[old_idx].hapticTargets : nullptr;
        std::vector<HapticTarget>* new_targets = (m_SelectedProfileIndex != -1) ? &m_Profiles[m_SelectedProfileIndex].hapticTargets : nullptr;
        outputMapper.SetActiveHapticTargets(new_targets);
    }

    float avail_width = ImGui::GetContentRegionAvail().x;
    float create_btn_width = ImGui::CalcTextSize("Create New").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float delete_btn_width = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float style_spacing = ImGui::GetStyle().ItemSpacing.x;

    float input_width = avail_width - create_btn_width - style_spacing;
    if (m_SelectedProfileIndex != -1) {
        input_width -= (delete_btn_width + style_spacing);
    }
    ImGui::SetNextItemWidth(input_width > 1.0f ? input_width : 1.0f);
    ImGui::InputTextWithHint("##NewProfileName", "New Profile Name", m_NewProfileName, sizeof(m_NewProfileName));
    ImGui::SameLine();
    if (ImGui::Button("Create New")) {
        if (strlen(m_NewProfileName) > 0) {
            MappingProfile new_profile;
            new_profile.name = m_NewProfileName;
            m_Profiles.push_back(new_profile);
            m_SelectedProfileIndex = m_Profiles.size() - 1;
            SaveProfile(new_profile);
            OutputMapper::GetInstance().SetActiveHapticTargets(&m_Profiles.back().hapticTargets);
            m_NewProfileName[0] = '\0';
        }
    }

    if (m_SelectedProfileIndex != -1) {
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            ImGui::OpenPopup("Delete Profile?");
        }

        if (ImGui::BeginPopupModal("Delete Profile?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to delete profile '%s'?", m_Profiles[m_SelectedProfileIndex].name.c_str());
            ImGui::Separator();

            if (ImGui::Button("Yes", ImVec2(120, 0))) {
                const auto& profile = m_Profiles[m_SelectedProfileIndex];
                try {
                    std::filesystem::path profilePath = GetMappingsDirectory();
                    profilePath /= (profile.name + ".json");
                    if (std::filesystem::exists(profilePath)) {
                        std::filesystem::remove(profilePath);
                    }
                    m_Profiles.erase(m_Profiles.begin() + m_SelectedProfileIndex);
                    m_SelectedProfileIndex = -1;
                    OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
                } catch (const std::exception& e) {
                    SDL_Log("Failed to delete profile: %s", e.what());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::Separator();

    // --- Mapping Configuration for Selected Profile ---
    if (m_SelectedProfileIndex != -1) {
        bool changed = false;
        MappingProfile &profile = m_Profiles[m_SelectedProfileIndex];

        ImGui::Text("'%s' Mappings", profile.name.c_str());

        if (ImGui::BeginTable("mappings", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Output Channel", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Input Source", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableHeadersRow();

            for (const auto &outputName : m_GenericOutputs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", outputName.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(outputName.c_str());

                InputSource &source = profile.outputToInput[outputName];

                std::string preview = "None";
                if (source.instance_id != 0) {
                    const auto& devices = m_DeviceManager.GetDevices();
                    auto it = std::find_if(devices.begin(), devices.end(),
                                           [&](const DeviceState& d) { return d.instance_id == source.instance_id; });
                    if (it != devices.end()) {
                        preview = it->name + " - Axis " + std::to_string(source.axisIndex);
                    }
                }

                if (source.axisIndex != -1) {
                    ImGuiStyle& style = ImGui::GetStyle();
                    float spacing = style.ItemSpacing.x;
                    float x_btn_width = ImGui::CalcTextSize("X").x + style.FramePadding.x * 2.0f;
                    float invert_width = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize("Invert").x;
                    float deadzone_width = 80.0f;
                    float range_width = 100.0f;
                    float extras_width = x_btn_width + spacing + invert_width + spacing + deadzone_width + spacing + range_width + spacing;
                    ImGui::SetNextItemWidth(-extras_width);
                } else {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                }

                if (ImGui::BeginCombo("##source", preview.c_str())) {
                    // Option for None
                    if (ImGui::Selectable("None", source.axisIndex == -1)) {
                        source = {};
                        changed = true;
                    }

                    for (const auto &dev : m_DeviceManager.GetDevices()) {
                        for (int i = 0; i < dev.num_axes; ++i) {
                            std::string selectable_label = dev.name + " - Axis " + std::to_string(i);
                            bool is_selected = (source.instance_id == dev.instance_id && source.axisIndex == i);
                            if (ImGui::Selectable(selectable_label.c_str(), is_selected)) {
                                source.deviceGuid = DeviceManager::GetDeviceGUIDString(dev);
                                source.instance_id = dev.instance_id;
                                source.axisIndex = i;
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                if (source.axisIndex != -1) {
                    ImGui::SameLine();
                    if (ImGui::Button("X")) {
                        source = {};
                        changed = true;
                    }
                    ImGui::SetItemTooltip("Clear mapping");
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Invert", &source.invert)) changed = true;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    if (ImGui::SliderFloat("Deadzone", &source.deadzone, 0.0f, 0.5f, "%.3f")) changed = true;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    const char *ranges[] = {"-1..1", "0..1", "-1..0"};
                    if (ImGui::Combo("Range", &source.outputRange, ranges, IM_ARRAYSIZE(ranges))) changed = true;
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (changed) {
            SaveProfile(profile);
        }

#ifdef ENABLE_EXCLUSIVE_INPUT
        ImGui::BeginDisabled(true); // Exclusive mode for multi-device profiles needs a robust implementation.
        bool exclusive = m_ExclusiveModeHandler.IsEnabled();
        if (ImGui::Checkbox("Exclusive Mode (Hide from other apps)", &exclusive)) {
            // m_ExclusiveModeHandler.SetEnabled(exclusive);
            // ApplyExclusiveMode();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Exclusive mode is not yet supported for multi-device profiles.\nThis requires platform-specific work to grab multiple devices.");
        }
        ImGui::EndDisabled();
#endif

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Button to Analog Mappings");
        ImGui::TextWrapped("Map buttons to analog outputs (e.g. set Throttle to 1.0 when a button is pressed).");

        if (ImGui::Button("Add Mapping")) {
            profile.buttonMappings.push_back(ButtonToAnalogMapping());
            SaveProfile(profile);
        }

        int mappingToDelete = -1;
        if (ImGui::BeginTable("ButtonMappingsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Device");
            ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Target Output", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("On Value", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Off Value", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < profile.buttonMappings.size(); ++i) {
                auto& mapping = profile.buttonMappings[i];
                bool mappingChanged = false;
                ImGui::PushID(1000 + i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                std::string currentDeviceName = "None";
                if (mapping.instance_id != 0) {
                     const auto& devices = m_DeviceManager.GetDevices();
                     auto it = std::find_if(devices.begin(), devices.end(), [&](const DeviceState& d){ return d.instance_id == mapping.instance_id; });
                     if (it != devices.end()) currentDeviceName = it->name;
                }

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##Device", currentDeviceName.c_str())) {
                    if (ImGui::Selectable("None", mapping.instance_id == 0)) {
                        mapping.device_guid = "";
                        mapping.instance_id = 0;
                        mappingChanged = true;
                    }
                    for (const auto& dev : m_DeviceManager.GetDevices()) {
                        if (ImGui::Selectable(dev.name.c_str(), mapping.instance_id == dev.instance_id)) {
                            mapping.device_guid = DeviceManager::GetDeviceGUIDString(dev);
                            mapping.instance_id = dev.instance_id;
                            mappingChanged = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##Button", &mapping.button_index)) mappingChanged = true;

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##Target", mapping.target_output_name.c_str())) {
                    for (const auto& output : m_GenericOutputs) {
                        if (ImGui::Selectable(output.c_str(), mapping.target_output_name == output)) {
                            mapping.target_output_name = output;
                            mappingChanged = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##OnVal", &mapping.on_value, 0.01f, -1.0f, 1.0f)) mappingChanged = true;

                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##OffVal", &mapping.off_value, 0.01f, -1.0f, 1.0f)) mappingChanged = true;

                ImGui::TableSetColumnIndex(5);
                if (ImGui::Button("Delete")) {
                    mappingToDelete = i;
                }

                if (mappingChanged) SaveProfile(profile);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (mappingToDelete != -1) {
            profile.buttonMappings.erase(profile.buttonMappings.begin() + mappingToDelete);
            SaveProfile(profile);
        }
    }

    ImGui::Separator();
    ImGui::Text("Output Preview:");
    std::string outputPreview = GetOutputPreview();
    ImGui::TextWrapped("%s", outputPreview.c_str());

    ImGui::End();
}

float InputMapper::ProcessAxis(const InputSource &config) {
    if (config.axisIndex < 0 || config.instance_id == 0)
        return 0.0f;

    SDL_Joystick *joystick = GetJoystickByID(config.instance_id, m_DeviceManager);
    if (!joystick) return 0.0f;

    Sint16 val = SDL_GetJoystickAxis(joystick, config.axisIndex);
    float norm;
    if (val < 0) {
        norm = static_cast<float>(val) / 32768.0f;
    } else {
        norm = static_cast<float>(val) / 32767.0f;
    }

    if (config.invert)
        norm = -norm;

    if (std::abs(norm) < config.deadzone)
        norm = 0.0f;
    else {
        // Rescale to account for deadzone
        if (norm > 0) {
            norm = (norm - config.deadzone) / (1.0f - config.deadzone);
        } else {
            norm = (norm + config.deadzone) / (1.0f - config.deadzone);
        }
    }

    float result = std::clamp(norm, -1.0f, 1.0f);
    if (config.outputRange == 1) { // 0 to 1
        result = (result + 1.0f) * 0.5f;
    } else if (config.outputRange == 2) { // -1 to 0
        result = (result - 1.0f) * 0.5f;
    }
    return result;
}

bool InputMapper::Update(bool dynamic_rate) {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= m_Profiles.size()) {
        return false;
    }

    const auto &profile = m_Profiles[m_SelectedProfileIndex];
    std::map<std::string, float> outputValues;

    // Initialize all outputs to 0
    for (const auto &outputName : m_GenericOutputs) {
        outputValues[outputName] = 0.0f;
    }

    // Process mapped inputs
    for (const auto &pair : profile.outputToInput) {
        const std::string &outputName = pair.first;
        const InputSource &source = pair.second;
        outputValues[outputName] = ProcessAxis(source);
    }

    // Process button mappings
    for (const auto& mapping : profile.buttonMappings) {
        if (mapping.instance_id == 0 || mapping.target_output_name.empty()) continue;
        SDL_Joystick* joystick = GetJoystickByID(mapping.instance_id, m_DeviceManager);
        if (!joystick) continue;

        if (SDL_GetJoystickButton(joystick, mapping.button_index)) {
            outputValues[mapping.target_output_name] = mapping.on_value;
        }
    }

    bool should_send = false;
    if (dynamic_rate) {
        const bool changed = (outputValues != m_LastOutputValues);
        const Uint64 currentTime = SDL_GetTicks();
        const bool sendDueToTimeout = (currentTime - m_LastBroadcastTime) >= 500;
        should_send = changed || sendDueToTimeout;
    } else {
        should_send = true;
    }

    if (should_send) {
        float steering = outputValues["Steering"];
        float throttle = outputValues["Throttle"];
        float brake = outputValues["Brake"];
        // float clutch = outputValues["Clutch"];
        // float handbrake = outputValues["Handbrake"];
        float pitch = outputValues["Pitch"];
        float roll = outputValues["Roll"];

        auto &websocket_server = WebSocketServer::GetInstance();
        if (websocket_server.IsRunning()) {
            websocket_server.Broadcast_wheel(steering, brake, throttle, pitch, roll);
        }

        auto &osc_server = OSCServer::GetInstance();
        if (osc_server.IsRunning()) {
            osc_server.SendWheel(steering, brake, throttle, pitch, roll);
        }
        m_LastOutputValues = outputValues;
        m_LastBroadcastTime = SDL_GetTicks();
    }
    
    return should_send;
}

std::string InputMapper::GetOutputPreview() {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= m_Profiles.size()) {
        return "No active profile selected.";
    }

    const auto &profile = m_Profiles[m_SelectedProfileIndex];
    std::map<std::string, float> outputValues;

    for (const auto &outputName : m_GenericOutputs) {
        outputValues[outputName] = 0.0f;
    }

    for (const auto &pair : profile.outputToInput) {
        const std::string &outputName = pair.first;
        const InputSource &source = pair.second;
        outputValues[outputName] = ProcessAxis(source);
    }

    std::string preview;
    for(const auto& pair : outputValues) {
        preview += pair.first + ": " + std::to_string(pair.second) + "\n";
    }

    if (!profile.buttonMappings.empty()) {
        preview += "\nButton Mappings:\n";
        for (const auto& mapping : profile.buttonMappings) {
            float val = mapping.off_value;
            if (mapping.instance_id != 0) {
                SDL_Joystick* joystick = GetJoystickByID(mapping.instance_id, m_DeviceManager);
                if (joystick && SDL_GetJoystickButton(joystick, mapping.button_index)) {
                    val = mapping.on_value;
                }
            }
            preview += "Target " + mapping.target_output_name + ": " + std::to_string(val) + "\n";
        }
    }

    return preview;
}

void InputMapper::LoadProfiles() {
    m_Profiles.clear();
    try {
        std::filesystem::path profileDir = GetMappingsDirectory();
        if (!std::filesystem::exists(profileDir)) {
            std::filesystem::create_directories(profileDir);
        }

        for (const auto &entry : std::filesystem::directory_iterator(profileDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::ifstream f(entry.path());
                if (f.is_open()) {
                    try {
                        json data = json::parse(f);
                        if (!data.contains("name") || !data["name"].is_string() || data["name"].get<std::string>().empty()) {
                            SDL_Log("Skipping profile with no name: %s", entry.path().string().c_str());
                            continue;
                        }
                        MappingProfile profile;
                        profile.name = data["name"];
                        for (auto& [key, val] : data["mappings"].items()) {
                            InputSource source;
                            source.deviceGuid = val.value("device_guid", "");
                            source.axisIndex = val.value("axis", -1);
                            source.invert = val.value("invert", false);
                            source.deadzone = val.value("deadzone", 0.05f);
                            source.outputRange = val.value("range", 0);
                            profile.outputToInput[key] = source;
                        }
                        if (data.contains("haptic_targets")) {
                            for (const auto& item : data["haptic_targets"]) {
                                HapticTarget target;
                                target.virtual_id = item.value("virtual_id", 0);
                                target.name = item.value("name", "Target");
                                target.device_guid = item.value("device_guid", "");
                                target.enable_rumble = item.value("enable_rumble", true);
                                target.enable_constant = item.value("enable_constant", true);
                                target.enable_periodic = item.value("enable_periodic", true);
                                target.enable_condition = item.value("enable_condition", true);
                                profile.hapticTargets.push_back(target);
                            }
                        }
                        if (data.contains("button_mappings")) {
                            for (const auto& item : data["button_mappings"]) {
                                ButtonToAnalogMapping mapping;
                                mapping.device_guid = item.value("device_guid", "");
                                mapping.button_index = item.value("button_index", 0);
                                mapping.target_output_name = item.value("target_output_name", "");
                                mapping.on_value = item.value("on_value", 1.0f);
                                mapping.off_value = item.value("off_value", 0.0f);
                                profile.buttonMappings.push_back(mapping);
                            }
                        }
                        m_Profiles.push_back(profile);
                    } catch (const std::exception& e) {
                        SDL_Log("Failed to parse profile %s: %s", entry.path().string().c_str(), e.what());
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        SDL_Log("Failed to load profiles: %s", e.what());
    }
}

void InputMapper::SaveProfile(const MappingProfile &profile) const {
    try {
        std::filesystem::path profileDir = GetMappingsDirectory();
        if (!std::filesystem::exists(profileDir)) {
            std::filesystem::create_directories(profileDir);
        }
        std::filesystem::path profilePath = profileDir / (profile.name + ".json");

        json data;
        data["name"] = profile.name;
        data["mappings"] = json::object();

        for (const auto& [key, val] : profile.outputToInput) {
            if (val.axisIndex != -1) {
                data["mappings"][key] = {
                    {"device_guid", val.deviceGuid},
                    {"axis", val.axisIndex},
                    {"invert", val.invert},
                    {"deadzone", val.deadzone},
                    {"range", val.outputRange}
                };
            }
        }

        data["haptic_targets"] = json::array();
        for (const auto& target : profile.hapticTargets) {
            json item;
            item["virtual_id"] = target.virtual_id;
            item["name"] = target.name;
            item["device_guid"] = target.device_guid;
            item["enable_rumble"] = target.enable_rumble;
            item["enable_constant"] = target.enable_constant;
            item["enable_periodic"] = target.enable_periodic;
            item["enable_condition"] = target.enable_condition;
            data["haptic_targets"].push_back(item);
        }

        data["button_mappings"] = json::array();
        for (const auto& mapping : profile.buttonMappings) {
            json item;
            item["device_guid"] = mapping.device_guid;
            item["button_index"] = mapping.button_index;
            item["target_output_name"] = mapping.target_output_name;
            item["on_value"] = mapping.on_value;
            item["off_value"] = mapping.off_value;
            data["button_mappings"].push_back(item);
        }

        std::ofstream o(profilePath);
        if (o.is_open()) {
            o << data.dump(4);
        }
    } catch (const std::exception& e) {
        SDL_Log("Failed to save profile: %s", e.what());
    }
}

std::vector<HapticTarget>* InputMapper::GetCurrentHapticTargets() {
    if (m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < m_Profiles.size()) {
        return &m_Profiles[m_SelectedProfileIndex].hapticTargets;
    }
    return nullptr;
}

void InputMapper::HandleDeviceConnectionChange() {
    const auto &devices = m_DeviceManager.GetDevices();

    std::map<std::string, SDL_JoystickID> guidToInstanceId;
    for (const auto &dev : devices) {
        guidToInstanceId[DeviceManager::GetDeviceGUIDString(dev)] = dev.instance_id;
    }

    for (auto &profile : m_Profiles) {
        for (auto &pair : profile.outputToInput) {
            auto &source = pair.second;
            auto it = guidToInstanceId.find(source.deviceGuid);
            if (it != guidToInstanceId.end()) {
                source.instance_id = it->second;
            } else {
                source.instance_id = 0; // Device not connected
            }
        }
    }

    for (auto &profile : m_Profiles) {
        for (auto &target : profile.hapticTargets) {
            if (target.device_guid.empty()) continue;
            auto it = guidToInstanceId.find(target.device_guid);
            if (it != guidToInstanceId.end()) {
                target.instance_id = it->second;
            } else if (target.instance_id != 0) {
                target.instance_id = 0;
            }
        }

        for (auto& mapping : profile.buttonMappings) {
            if (mapping.device_guid.empty()) continue;
            auto it = guidToInstanceId.find(mapping.device_guid);
            if (it != guidToInstanceId.end()) {
                mapping.instance_id = it->second;
            } else {
                mapping.instance_id = 0;
            }
        }
    }
}

#ifdef ENABLE_EXCLUSIVE_INPUT
void InputMapper::ApplyExclusiveMode() {
    // This needs a more robust implementation that can handle multiple devices across different platforms.
    // The current InputExclusiveMode implementation has limitations (e.g., Windows only supports one device at a time).
    // For now, we will not implement this to avoid platform-specific issues.
}
#endif
