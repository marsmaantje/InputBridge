#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "Preferences/Preferences.h"
#include "Network/WebSocketServer.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace { // Anonymous namespace for private helper functions

SDL_Joystick* GetSelectedJoystick(SDL_JoystickID selectedId, const DeviceManager& deviceManager) {
    if (selectedId == 0) {
        return nullptr;
    }
    const auto& devices = deviceManager.GetDevices();
    auto it = std::find_if(devices.begin(), devices.end(), 
        [selectedId](const DeviceState& dev) { return dev.instance_id == selectedId; });
    
    return (it != devices.end()) ? it->joystick : nullptr;
}

} // namespace

InputMapper::InputMapper(const DeviceManager& deviceManager)
    : m_DeviceManager(deviceManager) {}

static void DrawAxisConfig(const char* label, InputMapper::AxisConfig& config, int numAxes) {
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::BeginCombo("##axis", config.axisIndex == -1 ? "None" : std::to_string(config.axisIndex).c_str())) {
        if (ImGui::Selectable("None", config.axisIndex == -1)) config.axisIndex = -1;
        for (int i = 0; i < numAxes; i++) {
            if (ImGui::Selectable(std::to_string(i).c_str(), config.axisIndex == i)) config.axisIndex = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Inv", &config.invert);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("Deadzone", &config.deadzone, 0.0f, 0.5f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char* ranges[] = { "-1..1", "0..1", "-1..0" };
    ImGui::Combo("Range", &config.outputRange, ranges, IM_ARRAYSIZE(ranges));
    ImGui::PopID();
}

void InputMapper::DrawUI() {
    ImGui::Begin("Input Mapper");
    
    const auto& devices = m_DeviceManager.GetDevices();
    const DeviceState* selectedDeviceState = nullptr;

    // Device Selector
    const char* currentDeviceName = "None";
    if (m_SelectedDeviceID != 0) {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [this](const DeviceState& dev) { return dev.instance_id == m_SelectedDeviceID; });
        if (it != devices.end()) {
            selectedDeviceState = &*it;
            currentDeviceName = selectedDeviceState->name.c_str();
        } else {
            m_SelectedDeviceID = 0; // Device was disconnected
        }
    }

    if (ImGui::BeginCombo("Source Device", currentDeviceName)) {
        if (ImGui::Selectable("None", m_SelectedDeviceID == 0)) m_SelectedDeviceID = 0;
        for (const auto& dev : devices) {
            bool isSelected = (m_SelectedDeviceID == dev.instance_id);
            std::string label = dev.name + "##" + std::to_string(dev.instance_id);
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                m_SelectedDeviceID = dev.instance_id;
                selectedDeviceState = &dev;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (selectedDeviceState) {
        ImGui::Separator();
        ImGui::Text("Axis Mapping");
        DrawAxisConfig("Steering", m_Steering, selectedDeviceState->num_axes);
        DrawAxisConfig("Throttle", m_Throttle, selectedDeviceState->num_axes);
        DrawAxisConfig("Brake", m_Brake, selectedDeviceState->num_axes);
        DrawAxisConfig("Clutch", m_Clutch, selectedDeviceState->num_axes);
        DrawAxisConfig("Handbrake", m_Handbrake, selectedDeviceState->num_axes);
    }

    ImGui::Separator();
    ImGui::Text("Output Preview:");
    std::string outputPreview = UpdateAndBroadcastMessage();
    ImGui::TextWrapped("%s", outputPreview.c_str());

    ImGui::End();
    
    WebSocketServer::GetInstance().DrawUI();
}

float InputMapper::ProcessAxis(SDL_Joystick* joystick, const AxisConfig& config) {
    if (config.axisIndex < 0) return 0.0f;
    
    Sint16 val = SDL_GetJoystickAxis(joystick, config.axisIndex);
    float norm;
    if (val < 0) {
        norm = static_cast<float>(val) / 32768.0f;
    } else {
        norm = static_cast<float>(val) / 32767.0f;
    }
    
    if (config.invert) norm = -norm;
    
    if (std::abs(norm) < config.deadzone) norm = 0.0f;
    
    float result = std::clamp(norm, -1.0f, 1.0f);
    if (config.outputRange == 1) { // 0 to 1
        result = (result + 1.0f) * 0.5f;
    } else if (config.outputRange == 2) { // -1 to 0
        result = (result - 1.0f) * 0.5f;
    }
    return result;
}

std::string InputMapper::UpdateAndBroadcastMessage() {
    SDL_Joystick* joystick = GetSelectedJoystick(m_SelectedDeviceID, m_DeviceManager);

    if (!joystick) return "";

    float steering = ProcessAxis(joystick, m_Steering);
    float throttle = ProcessAxis(joystick, m_Throttle);
    float brake = ProcessAxis(joystick, m_Brake);
    float clutch = ProcessAxis(joystick, m_Clutch);
    float handbrake = ProcessAxis(joystick, m_Handbrake);

    auto& server = WebSocketServer::GetInstance();
    server.Broadcast_wheel(steering, brake, throttle);

    server.Broadcast("/wheel/steer", steering);
    server.Broadcast("/wheel/throttle", throttle);
    server.Broadcast("/wheel/brake", brake);
    if (m_Clutch.axisIndex != -1) server.Broadcast("/wheel/clutch", clutch);
    server.Broadcast("/wheel/handbrake", handbrake);

    int buttons_mask = 0;
    int num_buttons = SDL_GetNumJoystickButtons(joystick);
    for (int i = 0; i < num_buttons && i < 32; ++i) {
        if (SDL_GetJoystickButton(joystick, i)) buttons_mask |= (1 << i);
    }
    server.Broadcast("/wheel/buttons", buttons_mask);

    return "Broadcasting...";
}

void InputMapper::LoadConfig(const PreferencesManager& prefs) {
    std::string deviceGUID = prefs.GetString("InputMapper.DeviceGUID");
    if (!deviceGUID.empty()) {
        const auto& devices = m_DeviceManager.GetDevices();
        for (const auto& dev : devices) {
            if (DeviceManager::GetDeviceGUIDString(dev) == deviceGUID) {
                m_SelectedDeviceID = dev.instance_id;
                break;
            }
        }
    }

    auto LoadAxis = [&](const char* prefix, AxisConfig& config) {
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

#ifdef ENABLE_WEBSOCKETS
    int wsPort = prefs.GetInt("WebSocketServer.Port", 9001);
    if (!WebSocketServer::GetInstance().IsRunning() || WebSocketServer::GetInstance().GetPort() != wsPort) {
        WebSocketServer::GetInstance().Stop();
        WebSocketServer::GetInstance().Start(wsPort);
    }
#endif
}

void InputMapper::SaveConfig(PreferencesManager& prefs) const {
    if (m_SelectedDeviceID != 0) {
        const auto& devices = m_DeviceManager.GetDevices();
        for (const auto& dev : devices) {
            if (dev.instance_id == m_SelectedDeviceID) {
                prefs.SetString("InputMapper.DeviceGUID", DeviceManager::GetDeviceGUIDString(dev));
                break;
            }
        }
    } else {
        prefs.SetString("InputMapper.DeviceGUID", "");
    }

    auto SaveAxis = [&](const char* prefix, const AxisConfig& config) {
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

#ifdef ENABLE_WEBSOCKETS
    prefs.SetInt("WebSocketServer.Port", WebSocketServer::GetInstance().GetPort());
#endif
}
