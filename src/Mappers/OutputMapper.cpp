#include "OutputMapper.h"
#include "imgui.h"
#include "InputMapper.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
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
}

OutputMapper::OutputMapper(const DeviceManager& deviceManager) : m_DeviceManager(deviceManager) {
}

OutputMapper::~OutputMapper() {
    if (m_active_targets) {
        for (auto& target : *m_active_targets) {
            CloseHapticDevice(target);
        }
    }
}

void OutputMapper::SetActiveHapticTargets(std::vector<HapticTarget>* targets) {
    if (m_active_targets) {
        for (auto& target : *m_active_targets) {
            CloseHapticDevice(target);
        }
    }
    m_active_targets = targets;
    HandleDeviceConnectionChange();
}

void OutputMapper::DrawContent() {
    HandleDeviceConnectionChange();

    ImGui::Begin("Output Mapper");

    ImGui::Text("Haptic Output Targets");
    ImGui::Separator();
    ImGui::TextWrapped("Map virtual device IDs (used by external apps) to physical haptic devices.");
    ImGui::Spacing();

    auto& inputMapper = InputMapper::GetInstance();
    if (!m_active_targets) {
        ImGui::Text("No mapping profile selected in Input Mapper.");
        ImGui::End();
        return;
    }

    if (ImGui::Button("Add Target")) {
        HapticTarget newTarget;
        int maxId = -1;
        for(const auto& t : *m_active_targets) if(t.virtual_id > maxId) maxId = t.virtual_id;
        newTarget.virtual_id = maxId + 1;
        newTarget.name = "Target " + std::to_string(newTarget.virtual_id);
        m_active_targets->push_back(newTarget);
        inputMapper.SaveCurrentProfile();
    }

    int targetToDelete = -1;

    if (ImGui::BeginTable("HapticTargetsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Effects");
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < m_active_targets->size(); ++i) {
            auto& target = (*m_active_targets)[i];
            ImGui::PushID(i);

            ImGui::TableNextRow();

            // ID
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", target.virtual_id);

            // Device Selector
            ImGui::TableSetColumnIndex(1);
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
                    inputMapper.SaveCurrentProfile();
                }

                for (const auto& dev : m_DeviceManager.GetDevices()) {
                    if (SDL_IsJoystickHaptic(dev.joystick)) {
                        bool isSelected = (target.instance_id == dev.instance_id);
                        if (ImGui::Selectable(dev.name.c_str(), isSelected)) {
                            target.device_guid = DeviceManager::GetDeviceGUIDString(dev);
                            target.instance_id = dev.instance_id;
                            UpdateHapticDevice(target);
                            inputMapper.SaveCurrentProfile();
                        }
                    }
                }
                ImGui::EndCombo();
            }

            // Effects
            ImGui::TableSetColumnIndex(2);
            if (target.haptic_device) {
                unsigned int features = SDL_GetHapticFeatures(target.haptic_device);
                bool has_rumble = (features & SDL_HAPTIC_LEFTRIGHT) || SDL_HapticRumbleSupported(target.haptic_device);
                bool has_constant = (features & SDL_HAPTIC_CONSTANT);
                bool has_periodic = (features & SDL_HAPTIC_SINE) || (features & SDL_HAPTIC_TRIANGLE);
                bool has_condition = (features & SDL_HAPTIC_SPRING) || (features & SDL_HAPTIC_DAMPER);

                if (!has_rumble) ImGui::BeginDisabled();
                if (ImGui::Checkbox("Rumble", &target.enable_rumble)) inputMapper.SaveCurrentProfile();
                if (!has_rumble) ImGui::EndDisabled();
                ImGui::SameLine();

                if (!has_constant) ImGui::BeginDisabled();
                if (ImGui::Checkbox("Constant", &target.enable_constant)) inputMapper.SaveCurrentProfile();
                if (!has_constant) ImGui::EndDisabled();
                ImGui::SameLine();

                if (!has_periodic) ImGui::BeginDisabled();
                if (ImGui::Checkbox("Periodic", &target.enable_periodic)) inputMapper.SaveCurrentProfile();
                if (!has_periodic) ImGui::EndDisabled();
                ImGui::SameLine();

                if (!has_condition) ImGui::BeginDisabled();
                if (ImGui::Checkbox("Condition", &target.enable_condition)) inputMapper.SaveCurrentProfile();
                if (!has_condition) ImGui::EndDisabled();

            } else if (!target.device_guid.empty()) {
                ImGui::TextColored(ImVec4(1,0,0,1), "Missing");
            } else {
                ImGui::TextDisabled("Unmapped");
            }

            // Actions
            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button("Delete")) {
                targetToDelete = i;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (targetToDelete != -1) {
        CloseHapticDevice((*m_active_targets)[targetToDelete]);
        m_active_targets->erase(m_active_targets->begin() + targetToDelete);
        inputMapper.SaveCurrentProfile();
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
    if (!m_active_targets) return;

    const auto& devices = m_DeviceManager.GetDevices();
    std::map<std::string, SDL_JoystickID> guidMap;
    for (const auto& dev : devices) {
        guidMap[DeviceManager::GetDeviceGUIDString(dev)] = dev.instance_id;
    }

    for (auto& target : *m_active_targets) {
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

void OutputMapper::GetTargets(int virtual_id, std::vector<HapticTarget*>& out_targets) {
    if (!m_active_targets) return;
    for (auto& target : *m_active_targets) {
        if (target.virtual_id == virtual_id) out_targets.push_back(&target);
    }
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
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
    if (!target || !target->haptic_device || !target->enable_rumble) continue;

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
}

void OutputMapper::TriggerConstantForce(int virtual_id, float strength, int duration_ms) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
    if (!target || !target->haptic_device || !target->enable_constant) continue;

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
}

void OutputMapper::TriggerPeriodic(int virtual_id, float strength, int period, float magnitude, float offset, int phase, int duration_ms) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
    if (!target || !target->haptic_device || !target->enable_periodic) continue;

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
}

void OutputMapper::TriggerCondition(int virtual_id, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
    if (!target || !target->haptic_device || !target->enable_condition) continue;

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
}
