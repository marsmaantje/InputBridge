#include "InputMapper.h"
#include "Devices/DeviceManager.h"
#include "Preferences/Preferences.h"
#include "OSCGenerator.h"
#include "imgui.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>
#include <iostream>
#include "App.h"

#ifdef ENABLE_WEBSOCKETS
struct us_listen_socket_t;
struct WebSocketServerState {
    std::thread* thread = nullptr;
    uWS::App* app = nullptr;
    uWS::Loop* loop = nullptr;
    struct us_listen_socket_t* listen_socket = nullptr;
    std::mutex mutex;
    int port = 9001;
    bool running = false;
};

static WebSocketServerState g_WsServer;

static void StartWebSocketServer(int port) {
    std::lock_guard<std::mutex> lock(g_WsServer.mutex);
    if (g_WsServer.running) return;

    g_WsServer.port = port;
    g_WsServer.running = true;

    if (g_WsServer.thread) {
        delete g_WsServer.thread;
        g_WsServer.thread = nullptr;
    }

    g_WsServer.thread = new std::thread([port]() {
        uWS::App app;
        {
            std::lock_guard<std::mutex> lock(g_WsServer.mutex);
            g_WsServer.app = &app;
            g_WsServer.loop = uWS::Loop::get();
        }
        
        app.ws<int>("/*", {
            .open = [](auto *ws) { ws->subscribe("broadcast"); },
            .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
                std::cout << "FFB Data: " << message << std::endl;
            }
        }).listen(port, [](auto *listen_socket) {
            std::lock_guard<std::mutex> lock(g_WsServer.mutex);
            if (listen_socket) {
                std::cout << "WebSocket server listening on port " << g_WsServer.port << std::endl;
                g_WsServer.listen_socket = (struct us_listen_socket_t*)listen_socket;
            } else {
                std::cout << "Failed to listen on port " << g_WsServer.port << std::endl;
                g_WsServer.running = false;
            }
        }).run();

        {
            std::lock_guard<std::mutex> lock(g_WsServer.mutex);
            g_WsServer.app = nullptr;
            g_WsServer.loop = nullptr;
            g_WsServer.listen_socket = nullptr;
            g_WsServer.running = false;
        }
    });
    g_WsServer.thread->detach();
}

static void StopWebSocketServer() {
    std::lock_guard<std::mutex> lock(g_WsServer.mutex);
    if (g_WsServer.loop && g_WsServer.listen_socket) {
        struct us_listen_socket_t* socket = g_WsServer.listen_socket;
        g_WsServer.loop->defer([socket]() {
            us_listen_socket_close(0, socket);
        });
    }
}
#endif

static void BroadcastMessage(const std::string& msg, uWS::OpCode opCode) {
#ifdef ENABLE_WEBSOCKETS
    std::lock_guard<std::mutex> lock(g_WsServer.mutex);
    if (g_WsServer.loop) {
        g_WsServer.loop->defer([msg, opCode]() {
            if (g_WsServer.app) g_WsServer.app->publish("broadcast", msg, opCode);
        });
    }
#endif
}

InputMapper::InputMapper(const DeviceManager& deviceManager)
    : m_DeviceManager(deviceManager) {
#ifdef ENABLE_WEBSOCKETS
    StartWebSocketServer(9001);
#else
    std::cout << "WebSocket server disabled. Define ENABLE_WEBSOCKETS to enable." << std::endl;
#endif
}

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
    ImGui::Text("WebSocket Server");
#ifdef ENABLE_WEBSOCKETS
    static int port = 9001;
    ImGui::InputInt("Port", &port);
    
    bool isRunning = false;
    int currentPort = 0;
    {
        std::lock_guard<std::mutex> lock(g_WsServer.mutex);
        isRunning = g_WsServer.running;
        currentPort = g_WsServer.port;
    }

    if (isRunning) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Running on port %d", currentPort);
        ImGui::SameLine();
        if (ImGui::Button("Stop")) StopWebSocketServer();
    } else {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Stopped");
        ImGui::SameLine();
        if (ImGui::Button("Start")) StartWebSocketServer(port);
    }
#else
    ImGui::TextDisabled("Not available (ENABLE_WEBSOCKETS missing)");
#endif
    
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
    
    if (std::abs(norm) < config.deadzone) norm = 0.0f;
    
    float result = std::clamp(norm, -1.0f, 1.0f);
    if (config.outputRange == 1) { // 0 to 1
        result = (result + 1.0f) * 0.5f;
    } else if (config.outputRange == 2) { // -1 to 0
        result = (result - 1.0f) * 0.5f;
    }
    return result;
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
        if (!joystick) {
            BroadcastMessage("{}", uWS::OpCode::TEXT);
            return "{}";
        }

        std::stringstream ss;
        ss << "{";
        ss << "\"steering\":" << ProcessAxis(joystick, m_Steering) << ",";
        ss << "\"throttle\":" << ProcessAxis(joystick, m_Throttle) << ",";
        ss << "\"brake\":" << ProcessAxis(joystick, m_Brake) << ",";
        ss << "\"clutch\":" << ProcessAxis(joystick, m_Clutch) << ",";
        ss << "\"handbrake\":" << ProcessAxis(joystick, m_Handbrake);
        ss << "}";
        std::string msg = ss.str();
        BroadcastMessage(msg, uWS::OpCode::TEXT);
        return msg;
    } else if (m_OutputFormat == OutputFormat::WebsocketWheel) {
        if (!joystick) {
            BroadcastMessage("", uWS::OpCode::BINARY);
            return "";
        }
        std::stringstream ss;
        // Format: \x01<Steering>;\x02<Brake>;\x03<Throttle>;
        ss << "\x01" << ProcessAxis(joystick, m_Steering) << ";";
        ss << "\x02" << ProcessAxis(joystick, m_Brake) << ";";
        ss << "\x03" << ProcessAxis(joystick, m_Throttle) << ";";
        std::string msg = ss.str();
        BroadcastMessage(msg, uWS::OpCode::BINARY);
        return msg;
    } else {
        if (!joystick) {
            BroadcastMessage("", uWS::OpCode::BINARY);
            return "";
        }
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
        BroadcastMessage(msg, uWS::OpCode::BINARY);
        return msg;
    }
}

void InputMapper::LoadConfig(const PreferencesManager& prefs) {
    m_OutputFormat = (OutputFormat)prefs.GetInt("InputMapper.OutputFormat", (int)OutputFormat::JSON);

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
}

void InputMapper::SaveConfig(PreferencesManager& prefs) const {
    prefs.SetInt("InputMapper.OutputFormat", (int)m_OutputFormat);

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
}
