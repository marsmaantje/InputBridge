#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "OSCGenerator.h"
#include "imgui.h"
#include <sstream>
#include <algorithm>
#include <cmath>

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
    ImGui::PopID();
}

void InputMapper::DrawUI() {
    ImGui::Begin("Input Mapper");
    
    const auto& devices = m_DeviceManager.GetDevices();

    // Device Selector
    std::string currentDeviceName = "None";
    if (m_SelectedDeviceID != 0) {
        for (const auto& dev : devices) {
            if (dev.instance_id == m_SelectedDeviceID) {
                currentDeviceName = dev.name;
                break;
            }
        }
    }

    if (ImGui::BeginCombo("Output Format", m_OutputFormat == OutputFormat::JSON ? "JSON" : (m_OutputFormat == OutputFormat::WebsocketWheel ? "WebsocketWheel" : "OSC Resonite"))) {
        if (ImGui::Selectable("JSON", m_OutputFormat == OutputFormat::JSON)) m_OutputFormat = OutputFormat::JSON;
        if (ImGui::Selectable("WebsocketWheel", m_OutputFormat == OutputFormat::WebsocketWheel)) m_OutputFormat = OutputFormat::WebsocketWheel;
        if (ImGui::Selectable("OSC Resonite", m_OutputFormat == OutputFormat::OSC_Resonite)) m_OutputFormat = OutputFormat::OSC_Resonite;
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Source Device", currentDeviceName.c_str())) {
        if (ImGui::Selectable("None", m_SelectedDeviceID == 0)) m_SelectedDeviceID = 0;
        for (const auto& dev : devices) {
            bool isSelected = (m_SelectedDeviceID == dev.instance_id);
            std::string label = dev.name + "##" + std::to_string(dev.instance_id);
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                m_SelectedDeviceID = dev.instance_id;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (m_SelectedDeviceID != 0) {
        const DeviceState* devState = nullptr;
        for (const auto& dev : devices) {
            if (dev.instance_id == m_SelectedDeviceID) {
                devState = &dev;
                break;
            }
        }

        if (devState) {
            ImGui::Separator();
            ImGui::Text("Axis Mapping");
            DrawAxisConfig("Steering", m_Steering, devState->num_axes);
            DrawAxisConfig("Throttle", m_Throttle, devState->num_axes);
            DrawAxisConfig("Brake", m_Brake, devState->num_axes);
            DrawAxisConfig("Clutch", m_Clutch, devState->num_axes);
            DrawAxisConfig("Handbrake", m_Handbrake, devState->num_axes);
        }
    }
    
    ImGui::Separator();
    ImGui::Text("Output Preview:");
    std::string json = GenerateMessage();
    ImGui::TextWrapped("%s", json.c_str());

    ImGui::End();
}

float InputMapper::ProcessAxis(SDL_Joystick* joystick, const AxisConfig& config) {
    if (config.axisIndex < 0) return 0.0f;
    
    Sint16 val = SDL_GetJoystickAxis(joystick, config.axisIndex);
    float norm = (float)val / 32768.0f; // -1.0 to 1.0 approx
    
    if (config.invert) norm = -norm;
    
    if (std::abs(norm) < config.deadzone) return 0.0f;
    
    return std::clamp(norm, -1.0f, 1.0f);
}

std::string InputMapper::GenerateMessage() {
    SDL_Joystick* joystick = nullptr;
    if (m_SelectedDeviceID != 0) {
        const auto& devices = m_DeviceManager.GetDevices();
        for (const auto& dev : devices) {
            if (dev.instance_id == m_SelectedDeviceID) {
                joystick = dev.joystick;
                break;
            }
        }
    }

    if (m_OutputFormat == OutputFormat::JSON) {
        if (!joystick) return "{}";

        std::stringstream ss;
        ss << "{";
        ss << "\"steering\":" << ProcessAxis(joystick, m_Steering) << ",";
        ss << "\"throttle\":" << ProcessAxis(joystick, m_Throttle) << ",";
        ss << "\"brake\":" << ProcessAxis(joystick, m_Brake) << ",";
        ss << "\"clutch\":" << ProcessAxis(joystick, m_Clutch) << ",";
        ss << "\"handbrake\":" << ProcessAxis(joystick, m_Handbrake);
        ss << "}";
        return ss.str();
    } else if (m_OutputFormat == OutputFormat::WebsocketWheel) {
        if (!joystick) return "";
        std::stringstream ss;
        // Format: \x01<Steering>;\x02<Brake>;\x03<Throttle>;
        ss << "\x01" << ProcessAxis(joystick, m_Steering) << ";";
        ss << "\x02" << ProcessAxis(joystick, m_Brake) << ";";
        ss << "\x03" << ProcessAxis(joystick, m_Throttle) << ";";
        return ss.str();
    } else {
        if (!joystick) return "";
        std::string msg;
        msg += OSCGenerator::Message("/wheel/steer", ProcessAxis(joystick, m_Steering));
        msg += OSCGenerator::Message("/wheel/throttle", ProcessAxis(joystick, m_Throttle));
        msg += OSCGenerator::Message("/wheel/brake", ProcessAxis(joystick, m_Brake));
        if (m_Clutch.axisIndex != -1) {
            msg += OSCGenerator::Message("/wheel/clutch", ProcessAxis(joystick, m_Clutch));
        }
        int buttons_mask = 0;
        int num_buttons = SDL_GetNumJoystickButtons(joystick);
        for (int i = 0; i < num_buttons && i < 32; ++i) {
            if (SDL_GetJoystickButton(joystick, i)) buttons_mask |= (1 << i);
        }
        msg += OSCGenerator::Message("/wheel/buttons", buttons_mask);
        return msg;
    }
}
