#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "Network/WebSocketServer.h"
#include "Network/OSCServer.h"
#include "Preferences/Preferences.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace { // Anonymous namespace for private helper functions

SDL_Joystick *GetSelectedJoystick(SDL_JoystickID selectedId, const DeviceManager &deviceManager) {
    if (selectedId == 0) {
        return nullptr;
    }
    const auto &devices = deviceManager.GetDevices();
    auto it = std::find_if(devices.begin(), devices.end(), [selectedId](const DeviceState &dev) { return dev.instance_id == selectedId; });

    return (it != devices.end()) ? it->joystick : nullptr;
}

std::string SerializeAxisConfig(const InputMapper::AxisConfig &config) {
    return std::to_string(config.axisIndex) + "," + (config.invert ? "1" : "0") + "," + std::to_string(config.deadzone) + "," + std::to_string(config.outputRange);
}

void DeserializeAxisConfig(const std::string &data, InputMapper::AxisConfig &config) {
    if (data.empty()) return;
    size_t start = 0;
    auto get_next = [&](size_t &pos) {
        size_t end = data.find(',', pos);
        if (end == std::string::npos) end = data.length();
        std::string val = data.substr(pos, end - pos);
        pos = end + 1;
        return val;
    };
    try {
        config.axisIndex = std::stoi(get_next(start));
        config.invert = (std::stoi(get_next(start)) != 0);
        config.deadzone = std::stof(get_next(start));
        config.outputRange = std::stoi(get_next(start));
    } catch (...) {}
}

} // namespace

InputMapper::InputMapper(const DeviceManager &deviceManager) : m_DeviceManager(deviceManager) {}

static bool DrawAxisConfig(const char *label, InputMapper::AxisConfig &config, int numAxes) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::BeginCombo("##axis", config.axisIndex == -1 ? "None" : std::to_string(config.axisIndex).c_str())) {
        if (ImGui::Selectable("None", config.axisIndex == -1)) {
            if (config.axisIndex != -1) {
                config.axisIndex = -1;
                changed = true;
            }
        }
            config.axisIndex = -1;
        for (int i = 0; i < numAxes; i++) {
            if (ImGui::Selectable(std::to_string(i).c_str(), config.axisIndex == i)) {
                if (config.axisIndex != i) {
                config.axisIndex = i;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Inv", &config.invert)) changed = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::SliderFloat("Deadzone", &config.deadzone, 0.0f, 0.5f)) changed = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char *ranges[] = {"-1..1", "0..1", "-1..0"};
    if (ImGui::Combo("Range", &config.outputRange, ranges, IM_ARRAYSIZE(ranges))) changed = true;
    ImGui::PopID();
    return changed;
}

InputMapper::~InputMapper() {}

void InputMapper::DrawUI(PreferencesManager& prefs) {
    ImGui::Begin("Input Mapper");

    const auto &devices = m_DeviceManager.GetDevices();
    const DeviceState *selectedDeviceState = nullptr;

    // Device Selector
    const char *currentDeviceName = "None";
    if (m_SelectedDeviceID != 0) {
        auto it = std::find_if(devices.begin(), devices.end(), [this](const DeviceState &dev) { return dev.instance_id == m_SelectedDeviceID; });
        if (it != devices.end()) {
            selectedDeviceState = &*it;
            currentDeviceName = selectedDeviceState->name.c_str();
        } else {
            m_SelectedDeviceID = 0; // Device was disconnected
        }
    }

    if (ImGui::BeginCombo("Source Device", currentDeviceName)) {
        if (ImGui::Selectable("None", m_SelectedDeviceID == 0)) {
            m_SelectedDeviceID = 0;
            OSCServer::GetInstance().SetSelectedDevice(0);
            WebSocketServer::GetInstance().SetSelectedDevice(0);
        }

        for (const auto &dev : devices) {
            bool isSelected = (m_SelectedDeviceID == dev.instance_id);
            std::string label = dev.name + "##" + std::to_string(dev.instance_id);
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                m_SelectedDeviceID = dev.instance_id;
                selectedDeviceState = &dev;

                // Load settings for this device
                std::string guid = DeviceManager::GetDeviceGUIDString(dev);
                std::string mapping = prefs.GetDeviceMapping(guid);
                if (!mapping.empty()) {
                    std::string s = mapping;
                    size_t pos = 0;
                    while (pos < s.length()) {
                        size_t end = s.find(';', pos);
                        if (end == std::string::npos) end = s.length();
                        std::string token = s.substr(pos, end - pos);
                        pos = end + 1;
                        size_t colon = token.find(':');
                        if (colon != std::string::npos) {
                            std::string key = token.substr(0, colon);
                            std::string val = token.substr(colon + 1);
                            if (key == "Steering") DeserializeAxisConfig(val, m_Steering);
                            else if (key == "Throttle") DeserializeAxisConfig(val, m_Throttle);
                            else if (key == "Brake") DeserializeAxisConfig(val, m_Brake);
                            else if (key == "Clutch") DeserializeAxisConfig(val, m_Clutch);
                            else if (key == "Handbrake") DeserializeAxisConfig(val, m_Handbrake);
                            else if (key == "Pitch") DeserializeAxisConfig(val, m_Pitch);
                            else if (key == "Roll") DeserializeAxisConfig(val, m_Roll);
                        }
                    }
                } else {
                    // Reset to defaults if no mapping found
                    m_Steering = {}; m_Throttle = {}; m_Brake = {}; m_Clutch = {}; m_Handbrake = {}; m_Pitch = {}; m_Roll = {};
                }

#ifdef ENABLE_EXCLUSIVE_INPUT
                ApplyExclusiveMode();
#endif
                OSCServer::GetInstance().SetSelectedDevice(m_SelectedDeviceID);
                WebSocketServer::GetInstance().SetSelectedDevice(m_SelectedDeviceID);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

#ifdef ENABLE_EXCLUSIVE_INPUT
    bool exclusive = m_ExclusiveModeHandler.IsEnabled();
    if (ImGui::Checkbox("Exclusive Mode (Hide from other apps)", &exclusive)) {
        m_ExclusiveModeHandler.SetEnabled(exclusive);
        ApplyExclusiveMode();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Attempts to prevent other applications from receiving input from this device.\n"
                          "Note: This is platform dependent and may require administrative privileges.");
    }
#endif

    if (selectedDeviceState) {
        ImGui::Separator();
        ImGui::Text("Axis Mapping");
        bool changed = false;
        changed |= DrawAxisConfig("Steering", m_Steering, selectedDeviceState->num_axes);
        changed |= DrawAxisConfig("Throttle", m_Throttle, selectedDeviceState->num_axes);
        changed |= DrawAxisConfig("Brake", m_Brake, selectedDeviceState->num_axes);
        changed |= DrawAxisConfig("Clutch", m_Clutch, selectedDeviceState->num_axes);
        changed |= DrawAxisConfig("Handbrake", m_Handbrake, selectedDeviceState->num_axes);
        changed |= DrawAxisConfig("Pitch", m_Pitch, selectedDeviceState->num_axes);
        changed |= DrawAxisConfig("Roll", m_Roll, selectedDeviceState->num_axes);

        if (changed) {
            std::string guid = DeviceManager::GetDeviceGUIDString(*selectedDeviceState);
            std::string mapping;
            mapping += "Steering:" + SerializeAxisConfig(m_Steering) + ";";
            mapping += "Throttle:" + SerializeAxisConfig(m_Throttle) + ";";
            mapping += "Brake:" + SerializeAxisConfig(m_Brake) + ";";
            mapping += "Clutch:" + SerializeAxisConfig(m_Clutch) + ";";
            mapping += "Handbrake:" + SerializeAxisConfig(m_Handbrake) + ";";
            mapping += "Pitch:" + SerializeAxisConfig(m_Pitch) + ";";
            mapping += "Roll:" + SerializeAxisConfig(m_Roll) + ";";
            prefs.SetDeviceMapping(guid, mapping);
        }
    }

    ImGui::Separator();
    ImGui::Text("Output Preview:");
    std::string outputPreview = UpdateAndBroadcastMessage();
    ImGui::TextWrapped("%s", outputPreview.c_str());

    ImGui::End();
}

float InputMapper::ProcessAxis(SDL_Joystick *joystick, const AxisConfig &config) {
    if (config.axisIndex < 0)
        return 0.0f;

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

    float result = std::clamp(norm, -1.0f, 1.0f);
    if (config.outputRange == 1) { // 0 to 1
        result = (result + 1.0f) * 0.5f;
    } else if (config.outputRange == 2) { // -1 to 0
        result = (result - 1.0f) * 0.5f;
    }
    return result;
}

std::string InputMapper::UpdateAndBroadcastMessage() {
    SDL_Joystick *joystick = GetSelectedJoystick(m_SelectedDeviceID, m_DeviceManager);

    if (!joystick)
        return "";

    float steering = ProcessAxis(joystick, m_Steering);
    float throttle = ProcessAxis(joystick, m_Throttle);
    float brake = ProcessAxis(joystick, m_Brake);
    float clutch = ProcessAxis(joystick, m_Clutch);
    float handbrake = ProcessAxis(joystick, m_Handbrake);
    float pitch = ProcessAxis(joystick, m_Pitch);
    float roll = ProcessAxis(joystick, m_Roll);

    auto &websocket_server = WebSocketServer::GetInstance();
    if (websocket_server.IsRunning()) {
        websocket_server.Broadcast_wheel(steering, brake, throttle, pitch, roll);
    }

    auto &osc_server = OSCServer::GetInstance();
    if (osc_server.IsRunning()) {
        osc_server.SendWheel(steering, brake, throttle, pitch, roll);
        
        std::vector<uint32_t> buttons;
        int num_buttons = SDL_GetNumJoystickButtons(joystick);
        for (int i = 0; i < 4; ++i) {
            uint32_t mask = 0;
            for (int j = 0; j < 32; ++j) {
                int btn = i * 32 + j;
                if (btn < num_buttons && SDL_GetJoystickButton(joystick, btn))
                    mask |= (1U << j);
            }
            buttons.push_back(mask);
        }
        osc_server.SendButtons(buttons);
    }

    return "Broadcasting...";
}

void InputMapper::LoadConfig(const PreferencesManager &prefs) {
    std::string deviceGUID = prefs.GetString("InputMapper.DeviceGUID");
    if (!deviceGUID.empty()) {
        const auto &devices = m_DeviceManager.GetDevices();
        for (const auto &dev : devices) {
            if (DeviceManager::GetDeviceGUIDString(dev) == deviceGUID) {
                m_SelectedDeviceID = dev.instance_id;
                break;
            }
        }
    }
    OSCServer::GetInstance().SetSelectedDevice(m_SelectedDeviceID);
    WebSocketServer::GetInstance().SetSelectedDevice(m_SelectedDeviceID);

    auto LoadAxis = [&](const char *prefix, AxisConfig &config) {
        config.axisIndex = prefs.GetInt(std::string(prefix) + ".Axis", -1);
        config.invert = prefs.GetBool(std::string(prefix) + ".Invert", false);
        config.deadzone = prefs.GetFloat(std::string(prefix) + ".Deadzone", 0.05f);
        config.outputRange = prefs.GetInt(std::string(prefix) + ".Range", 0);
    };

    LoadAxis("InputMapper.Steering", m_Steering);
    LoadAxis("InputMapper.Throttle", m_Throttle);
    LoadAxis("InputMapper.Brake", m_Brake);
    LoadAxis("InputMapper.Clutch", m_Clutch);
    LoadAxis("InputMapper.Handbrake", m_Handbrake);
#ifdef ENABLE_EXCLUSIVE_INPUT
    m_ExclusiveModeHandler.SetEnabled(prefs.GetBool("InputMapper.ExclusiveMode", false));
#endif
#ifdef ENABLE_EXCLUSIVE_INPUT
    ApplyExclusiveMode();
#endif
}

void InputMapper::SaveConfig(PreferencesManager &prefs) const {
    if (m_SelectedDeviceID != 0) {
        const auto &devices = m_DeviceManager.GetDevices();
        for (const auto &dev : devices) {
            if (dev.instance_id == m_SelectedDeviceID) {
                prefs.SetString("InputMapper.DeviceGUID", DeviceManager::GetDeviceGUIDString(dev));
                break;
            }
        }
    } else {
        prefs.SetString("InputMapper.DeviceGUID", "");
    }

    auto SaveAxis = [&](const char *prefix, const AxisConfig &config) {
        prefs.SetInt(std::string(prefix) + ".Axis", config.axisIndex);
        prefs.SetBool(std::string(prefix) + ".Invert", config.invert);
        prefs.SetFloat(std::string(prefix) + ".Deadzone", config.deadzone);
        prefs.SetInt(std::string(prefix) + ".Range", config.outputRange);
    };

    SaveAxis("InputMapper.Steering", m_Steering);
    SaveAxis("InputMapper.Throttle", m_Throttle);
    SaveAxis("InputMapper.Brake", m_Brake);
    SaveAxis("InputMapper.Clutch", m_Clutch);
    SaveAxis("InputMapper.Handbrake", m_Handbrake);
#ifdef ENABLE_EXCLUSIVE_INPUT
    prefs.SetBool("InputMapper.ExclusiveMode", m_ExclusiveModeHandler.IsEnabled());
#endif
}

#ifdef ENABLE_EXCLUSIVE_INPUT
void InputMapper::ApplyExclusiveMode() {
    SDL_Joystick *joystick = GetSelectedJoystick(m_SelectedDeviceID, m_DeviceManager);
    if (!joystick)
        return;

    m_ExclusiveModeHandler.Apply(joystick);
}
#endif