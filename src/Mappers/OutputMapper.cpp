#include "OutputMapper.h"
#include "imgui.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_filesystem.h>

using json = nlohmann::json;

std::unique_ptr<OutputMapper> OutputMapper::s_Instance;

OutputMapper& OutputMapper::GetInstance()
{
    return *s_Instance;
}

void OutputMapper::Init(const DeviceManager& deviceManager)
{
    if (!s_Instance)
        s_Instance.reset(new OutputMapper(deviceManager));
}

void OutputMapper::Shutdown()
{
    s_Instance.reset();
}

namespace {
    SDL_Joystick* GetJoystickByID(SDL_JoystickID id, const DeviceManager& deviceManager) {
        if (id == 0) return nullptr;
        const auto& devices = deviceManager.GetDevices();
        auto it = std::find_if(devices.begin(), devices.end(), [id](const DeviceState& dev) { return dev.instance_id == id; });
        return (it != devices.end()) ? it->joystick : nullptr;
    }

    std::filesystem::path GetOutputConfigPath() {
        const char *basePath = SDL_GetBasePath();
        std::filesystem::path dir;
        if (basePath) {
            dir = std::filesystem::path(basePath) / "mappings";
        } else {
            dir = "mappings";
        }

        try {
            if (!std::filesystem::exists(dir)) {
                std::filesystem::create_directories(dir);
            }
        } catch (const std::exception& e) {
            SDL_Log("Failed to create output directory: %s", e.what());
        }
        return dir / "output_mappings.json";
    }
}

OutputMapper::OutputMapper(const DeviceManager& deviceManager) : m_DeviceManager(deviceManager) {
}

OutputMapper::~OutputMapper() {
    for (auto& target : m_Targets) {
        CloseHapticDevice(target);
    }
}

void OutputMapper::LoadConfig(PreferencesManager& prefs) {
    std::filesystem::path configPath = GetOutputConfigPath();
    if (std::filesystem::exists(configPath)) {
        std::ifstream f(configPath);
        if (f.is_open()) {
            try {
                json data = json::parse(f);
                m_Targets.clear();
                for (const auto& item : data["targets"]) {
                    HapticTarget target;
                    target.virtual_id = item.value("virtual_id", 0);
                    target.name = item.value("name", "Target");
                    target.device_guid = item.value("device_guid", "");
                    m_Targets.push_back(target);
                }
            } catch (const std::exception& e) {
                SDL_Log("Failed to load output mappings: %s", e.what());
            }
        }
    }
    HandleDeviceConnectionChange();
}

void OutputMapper::SaveConfig() const {
    json data;
    data["targets"] = json::array();
    for (const auto& target : m_Targets) {
        json item;
        item["virtual_id"] = target.virtual_id;
        item["name"] = target.name;
        item["device_guid"] = target.device_guid;
        data["targets"].push_back(item);
    }

    std::ofstream o(GetOutputConfigPath());
    if (o.is_open()) {
        o << data.dump(4);
    }
}

void OutputMapper::DrawContent() {
    HandleDeviceConnectionChange();

    ImGui::Begin("Output Mapper");

    ImGui::Text("Haptic Output Targets");
    ImGui::Separator();
    ImGui::TextWrapped("Map virtual device IDs (used by external apps) to physical haptic devices.");
    ImGui::Spacing();

    if (ImGui::Button("Add Target")) {
        HapticTarget newTarget;
        int maxId = -1;
        for(const auto& t : m_Targets) if(t.virtual_id > maxId) maxId = t.virtual_id;
        newTarget.virtual_id = maxId + 1;
        newTarget.name = "Target " + std::to_string(newTarget.virtual_id);
        m_Targets.push_back(newTarget);
        SaveConfig();
    }

    int targetToDelete = -1;

    if (ImGui::BeginTable("HapticTargetsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Physical Device");
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < m_Targets.size(); ++i) {
            auto& target = m_Targets[i];
            ImGui::PushID(i);

            ImGui::TableNextRow();

            // ID
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", target.virtual_id);

            // Name
            ImGui::TableSetColumnIndex(1);
            char nameBuf[64];
            strncpy(nameBuf, target.name.c_str(), sizeof(nameBuf));
            nameBuf[sizeof(nameBuf)-1] = 0;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf))) {
                target.name = nameBuf;
                SaveConfig();
            }

            // Device Selector
            ImGui::TableSetColumnIndex(2);
            std::string currentDeviceName = "None";
            if (target.instance_id != 0) {
                 const auto& devices = m_DeviceManager.GetDevices();
                 auto it = std::find_if(devices.begin(), devices.end(), [&](const DeviceState& d){ return d.instance_id == target.instance_id; });
                 if (it != devices.end()) currentDeviceName = it->name;
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##Device", currentDeviceName.c_str())) {
                if (ImGui::Selectable("None", target.instance_id == 0)) {
                    target.device_guid = "";
                    target.instance_id = 0;
                    CloseHapticDevice(target);
                    SaveConfig();
                }

                for (const auto& dev : m_DeviceManager.GetDevices()) {
                    if (SDL_IsJoystickHaptic(dev.joystick)) {
                        bool isSelected = (target.instance_id == dev.instance_id);
                        if (ImGui::Selectable(dev.name.c_str(), isSelected)) {
                            target.device_guid = DeviceManager::GetDeviceGUIDString(dev);
                            target.instance_id = dev.instance_id;
                            UpdateHapticDevice(target);
                            SaveConfig();
                        }
                    }
                }
                ImGui::EndCombo();
            }

            // Status
            ImGui::TableSetColumnIndex(3);
            if (target.haptic_device) {
                ImGui::TextColored(ImVec4(0,1,0,1), "Active");
            } else if (!target.device_guid.empty()) {
                ImGui::TextColored(ImVec4(1,0,0,1), "Missing");
            } else {
                ImGui::TextDisabled("Unmapped");
            }

            // Actions
            ImGui::TableSetColumnIndex(4);
            if (ImGui::Button("Delete")) {
                targetToDelete = i;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (targetToDelete != -1) {
        CloseHapticDevice(m_Targets[targetToDelete]);
        m_Targets.erase(m_Targets.begin() + targetToDelete);
        SaveConfig();
    }

    ImGui::End();
}

void OutputMapper::Update() {
    std::vector<HapticCommand> queue;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        queue.swap(m_CommandQueue);
    }

    for (const auto& cmd : queue) {
        switch (cmd.type) {
            case HapticCommand::RUMBLE:
                TriggerRumble(cmd.virtual_id, cmd.fParams[0], cmd.fParams[1], cmd.iParams[0]);
                break;
            case HapticCommand::CONSTANT:
                TriggerConstantForce(cmd.virtual_id, cmd.fParams[0], cmd.iParams[0]);
                break;
            case HapticCommand::PERIODIC:
                TriggerPeriodic(cmd.virtual_id, cmd.fParams[0], cmd.iParams[0], cmd.fParams[1], cmd.fParams[2], cmd.iParams[1], cmd.iParams[2]);
                break;
            case HapticCommand::CONDITION:
                TriggerCondition(cmd.virtual_id, cmd.fParams[0], cmd.fParams[1], cmd.fParams[2], cmd.fParams[3], cmd.fParams[4], cmd.fParams[5], cmd.iParams[0]);
                break;
        }
    }
}

void OutputMapper::HandleDeviceConnectionChange() {
    const auto& devices = m_DeviceManager.GetDevices();
    std::map<std::string, SDL_JoystickID> guidMap;
    for (const auto& dev : devices) {
        guidMap[DeviceManager::GetDeviceGUIDString(dev)] = dev.instance_id;
    }

    for (auto& target : m_Targets) {
        if (target.device_guid.empty()) continue;

        auto it = guidMap.find(target.device_guid);
        if (it != guidMap.end()) {
            if (target.instance_id != it->second) {
                target.instance_id = it->second;
                UpdateHapticDevice(target);
            } else if (target.haptic_device == nullptr) {
                UpdateHapticDevice(target);
            }
        } else {
            if (target.instance_id != 0) {
                target.instance_id = 0;
                CloseHapticDevice(target);
            }
        }
    }
}

void OutputMapper::UpdateHapticDevice(HapticTarget& target) {
    CloseHapticDevice(target);
    if (target.instance_id == 0) return;

    SDL_Joystick* joystick = GetJoystickByID(target.instance_id, m_DeviceManager);
    if (joystick && SDL_IsJoystickHaptic(joystick)) {
        target.haptic_device = SDL_OpenHapticFromJoystick(joystick);
        if (target.haptic_device) {
            if (SDL_HapticRumbleSupported(target.haptic_device)) {
                SDL_InitHapticRumble(target.haptic_device);
            }
        }
    }
}

void OutputMapper::CloseHapticDevice(HapticTarget& target) {
    if (target.haptic_device) {
        SDL_CloseHaptic(target.haptic_device);
        target.haptic_device = nullptr;
        target.constant_effect_id = -1;
        target.periodic_effect_id = -1;
        target.condition_effect_id = -1;
        target.rumble_effect_id = -1;
    }
}

HapticTarget* OutputMapper::GetTarget(int virtual_id) {
    for (auto& target : m_Targets) {
        if (target.virtual_id == virtual_id) return &target;
    }
    return nullptr;
}

// --- Queue Methods ---

void OutputMapper::QueueRumble(int virtual_id, float low_freq, float high_freq, int duration_ms) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    HapticCommand cmd;
    cmd.type = HapticCommand::RUMBLE;
    cmd.virtual_id = virtual_id;
    cmd.fParams[0] = low_freq;
    cmd.fParams[1] = high_freq;
    cmd.iParams[0] = duration_ms;
    m_CommandQueue.push_back(cmd);
}

void OutputMapper::QueueConstantForce(int virtual_id, float strength, int duration_ms) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    HapticCommand cmd;
    cmd.type = HapticCommand::CONSTANT;
    cmd.virtual_id = virtual_id;
    cmd.fParams[0] = strength;
    cmd.iParams[0] = duration_ms;
    m_CommandQueue.push_back(cmd);
}

void OutputMapper::QueuePeriodic(int virtual_id, float strength, int period, float magnitude, float offset, int phase, int duration_ms) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    HapticCommand cmd;
    cmd.type = HapticCommand::PERIODIC;
    cmd.virtual_id = virtual_id;
    cmd.fParams[0] = strength;
    cmd.iParams[0] = period;
    cmd.fParams[1] = magnitude;
    cmd.fParams[2] = offset;
    cmd.iParams[1] = phase;
    cmd.iParams[2] = duration_ms;
    m_CommandQueue.push_back(cmd);
}

void OutputMapper::QueueCondition(int virtual_id, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    HapticCommand cmd;
    cmd.type = HapticCommand::CONDITION;
    cmd.virtual_id = virtual_id;
    cmd.fParams[0] = right_sat;
    cmd.fParams[1] = left_sat;
    cmd.fParams[2] = right_coeff;
    cmd.fParams[3] = left_coeff;
    cmd.fParams[4] = deadband;
    cmd.fParams[5] = center;
    cmd.iParams[0] = duration_ms;
    m_CommandQueue.push_back(cmd);
}

// --- Trigger Implementations ---

void OutputMapper::TriggerRumble(int virtual_id, float low_freq, float high_freq, int duration_ms) {
    HapticTarget* target = GetTarget(virtual_id);
    if (!target || !target->haptic_device) return;

    if (SDL_GetHapticFeatures(target->haptic_device) & SDL_HAPTIC_LEFTRIGHT) {
        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = duration_ms;
        effect.leftright.large_magnitude = (Uint16)(std::clamp(low_freq, 0.0f, 1.0f) * 65535.0f);
        effect.leftright.small_magnitude = (Uint16)(std::clamp(high_freq, 0.0f, 1.0f) * 65535.0f);

        if (target->rumble_effect_id == -1) {
             target->rumble_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
        } else {
             SDL_UpdateHapticEffect(target->haptic_device, target->rumble_effect_id, &effect);
        }

        if (target->rumble_effect_id != -1) {
            SDL_RunHapticEffect(target->haptic_device, target->rumble_effect_id, 1);
        }
    } else {
        float strength = std::max(low_freq, high_freq);
        SDL_PlayHapticRumble(target->haptic_device, strength, duration_ms);
    }
}

void OutputMapper::TriggerConstantForce(int virtual_id, float strength, int duration_ms) {
    HapticTarget* target = GetTarget(virtual_id);
    if (!target || !target->haptic_device) return;

    SDL_HapticEffect effect;
    SDL_memset(&effect, 0, sizeof(effect));
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = 1;
    effect.constant.length = duration_ms;
    effect.constant.level = (Sint16)(std::clamp(strength, -1.0f, 1.0f) * 32767.0f);

    if (target->constant_effect_id == -1) {
        target->constant_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
    } else {
        SDL_UpdateHapticEffect(target->haptic_device, target->constant_effect_id, &effect);
    }

    if (target->constant_effect_id != -1) {
        SDL_RunHapticEffect(target->haptic_device, target->constant_effect_id, 1);
    }
}

void OutputMapper::TriggerPeriodic(int virtual_id, float strength, int period, float magnitude, float offset, int phase, int duration_ms) {
    HapticTarget* target = GetTarget(virtual_id);
    if (!target || !target->haptic_device) return;

    SDL_HapticEffect effect;
    SDL_memset(&effect, 0, sizeof(effect));
    effect.type = SDL_HAPTIC_SINE;
    effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.periodic.direction.dir[0] = 1;
    effect.periodic.length = duration_ms;
    effect.periodic.period = (Uint16)period;
    effect.periodic.magnitude = (Sint16)(std::clamp(magnitude, -1.0f, 1.0f) * 32767.0f);
    effect.periodic.offset = (Sint16)(std::clamp(offset, -1.0f, 1.0f) * 32767.0f);
    effect.periodic.phase = (Uint16)std::clamp(phase, 0, 36000);

    if (strength != 1.0f) {
        effect.periodic.magnitude = (Sint16)(effect.periodic.magnitude * strength);
    }

    if (target->periodic_effect_id == -1) {
        target->periodic_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
    } else {
        SDL_UpdateHapticEffect(target->haptic_device, target->periodic_effect_id, &effect);
    }

    if (target->periodic_effect_id != -1) {
        SDL_RunHapticEffect(target->haptic_device, target->periodic_effect_id, 1);
    }
}

void OutputMapper::TriggerCondition(int virtual_id, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms) {
    HapticTarget* target = GetTarget(virtual_id);
    if (!target || !target->haptic_device) return;

    SDL_HapticEffect effect;
    SDL_memset(&effect, 0, sizeof(effect));
    effect.type = SDL_HAPTIC_SPRING;
    effect.condition.length = duration_ms;
    effect.condition.right_sat[0] = (Uint16)(std::clamp(right_sat, 0.0f, 1.0f) * 65535.0f);
    effect.condition.left_sat[0] = (Uint16)(std::clamp(left_sat, 0.0f, 1.0f) * 65535.0f);
    effect.condition.right_coeff[0] = (Sint16)(std::clamp(right_coeff, -1.0f, 1.0f) * 32767.0f);
    effect.condition.left_coeff[0] = (Sint16)(std::clamp(left_coeff, -1.0f, 1.0f) * 32767.0f);
    effect.condition.deadband[0] = (Uint16)(std::clamp(deadband, 0.0f, 1.0f) * 65535.0f);
    effect.condition.center[0] = (Sint16)(std::clamp(center, -1.0f, 1.0f) * 32767.0f);

    if (target->condition_effect_id == -1) {
        target->condition_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
    } else {
        SDL_UpdateHapticEffect(target->haptic_device, target->condition_effect_id, &effect);
    }

    if (target->condition_effect_id != -1) {
        SDL_RunHapticEffect(target->haptic_device, target->condition_effect_id, 1);
    }
}
