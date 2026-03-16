#include "OutputMapper.h"
#include "imgui.h"
#include "InputMapper.h"
#include "Haptics/HapticDevice.h"
#include <algorithm>
#include <memory>
#include <map>
#include <cstring>
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

bool OutputMapper::IsHapticsActive() const {
    // Consider haptics active for a short period after the last command.
    // This is an approximation for UI feedback.
    const uint64_t activity_timeout_ms = 500;
    return (SDL_GetTicks() - m_lastHapticActivityTime.load()) < activity_timeout_ms;
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
    DrawContentOnly();
    ImGui::End();
}

void OutputMapper::DrawContentOnly() {
    HandleDeviceConnectionChange();

    // ── Input Protocol Selection ──────────────────────────────────────────────
    InputMapper::GetInstance().DrawInputProtocolSelector();
    ImGui::Separator();

    ImGui::Text("Haptic Output Targets");
    ImGui::Separator();
    ImGui::TextWrapped("Map virtual device IDs (used by external apps) to physical haptic devices.");
    ImGui::Spacing();

    auto& inputMapper = InputMapper::GetInstance();
    if (!m_active_targets) {
        ImGui::Text("No mapping profile selected in Input Mapper.");
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
                    if (SDL_IsJoystickHaptic(dev.joystick) || SDL_IsGamepad(dev.instance_id)) {
                        bool isSelected = (target.instance_id == dev.instance_id);
                        bool isUsed = false;
                        for (const auto& other_target : *m_active_targets) {
                            if (&other_target != &target && other_target.instance_id == dev.instance_id) {
                                isUsed = true;
                                break;
                            }
                        }

                        if (isUsed) {
                            ImGui::BeginDisabled();
                            ImGui::Selectable((dev.name + " (Used)").c_str(), false);
                            ImGui::EndDisabled();
                        } else {
                            if (ImGui::Selectable(dev.name.c_str(), isSelected)) {
                                target.device_guid = DeviceManager::GetDeviceGUIDString(dev);
                                target.instance_id = dev.instance_id;
                                UpdateHapticDevice(target);
                                inputMapper.SaveCurrentProfile();
                            }
                        }
                    }
                }
                ImGui::EndCombo();
            }
            if (!target.status_message.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", target.status_message.c_str());
            }

            // Effects
            ImGui::TableSetColumnIndex(2);
            if (target.haptic_device || (target.instance_id != 0 && SDL_IsGamepad(target.instance_id))) {
                bool has_rumble = false;
                bool has_constant = false;
                bool has_periodic = false;
                bool has_condition = false;

                if (target.haptic_device) {
                    unsigned int features = SDL_GetHapticFeatures(target.haptic_device);
                    has_rumble = (features & SDL_HAPTIC_LEFTRIGHT) || SDL_HapticRumbleSupported(target.haptic_device);
                    has_constant = (features & SDL_HAPTIC_CONSTANT);
                    has_periodic = (features & SDL_HAPTIC_SINE) || (features & SDL_HAPTIC_TRIANGLE);
                    has_condition = (features & SDL_HAPTIC_SPRING) || (features & SDL_HAPTIC_DAMPER);
                } else {
                    // Assume Gamepad supports rumble
                    has_rumble = true;
                }

                ImGuiStyle& style = ImGui::GetStyle();
                float avail_width = ImGui::GetContentRegionAvail().x;
                float current_line_width = 0.0f;

                auto DrawEffect = [&](const char* label, bool* v, bool supported) {
                    float item_w = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
                    if (current_line_width > 0.0f) {
                        if (current_line_width + style.ItemSpacing.x + item_w > avail_width) current_line_width = 0.0f;
                        else { ImGui::SameLine(); current_line_width += style.ItemSpacing.x; }
                    }

                    if (!supported && *v) {
                        *v = false;
                        inputMapper.SaveCurrentProfile();
                    }

                    if (!supported) ImGui::BeginDisabled();
                    if (ImGui::Checkbox(label, v)) inputMapper.SaveCurrentProfile();
                    if (!supported) ImGui::EndDisabled();
                    current_line_width += item_w;
                };

                DrawEffect("Rumble", &target.enable_rumble, has_rumble);
                DrawEffect("Constant", &target.enable_constant, has_constant);
                DrawEffect("Periodic", &target.enable_periodic, has_periodic);
                DrawEffect("Condition", &target.enable_condition, has_condition);

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
                TriggerRumble(cmd.virtual_id, cmd.iParams[0], cmd.fParams[0], cmd.fParams[1], cmd.iParams[1]);
                break;
            case HapticCommand::CONSTANT:
                TriggerConstantForce(cmd.virtual_id, cmd.iParams[0], cmd.fParams[0], cmd.iParams[1]);
                break;
            case HapticCommand::PERIODIC:
                TriggerPeriodic(cmd.virtual_id, cmd.iParams[0], cmd.fParams[0], cmd.iParams[1], cmd.fParams[1], cmd.fParams[2], cmd.iParams[2], cmd.iParams[3]);
                break;
            case HapticCommand::CONDITION:
                TriggerCondition(cmd.virtual_id, cmd.iParams[0], static_cast<HapticConditionType>(cmd.iParams[1]), cmd.fParams[0], cmd.fParams[1], cmd.fParams[2], cmd.fParams[3], cmd.fParams[4], cmd.fParams[5], cmd.iParams[2]);
                break;
            case HapticCommand::GAIN:
                TriggerSetGain(cmd.virtual_id, cmd.iParams[0]);
                break;
            case HapticCommand::DUALSENSE_TRIGGER:
                TriggerDualSenseTrigger(cmd.virtual_id, cmd.sParams[0], cmd.sParams[1],
                                       cmd.iParams[0], cmd.iParams[1], cmd.iParams[2],
                                       cmd.iParams[3], cmd.iParams[4], cmd.iParams[5],
                                       cmd.iParams[6], cmd.iParams[7], cmd.iParams[8],
                                       cmd.iParams[9] & 0xFF, (cmd.iParams[9] >> 8) & 0xFF);
                break;
        }
    }
}

void OutputMapper::StopAllHapticEffects()
{
    // Stop effects on OutputMapper-managed targets (output mapping UI slots).
    if (m_active_targets) {
        for (auto& target : *m_active_targets) {
            if (target.haptic_device) {
                SDL_StopHapticEffects(target.haptic_device);
            }
            // For gamepads that don't use the haptic_device path for rumble
            else if (target.instance_id != 0 && SDL_IsGamepad(target.instance_id)) {
                SDL_Gamepad* pad = SDL_GetGamepadFromID(target.instance_id);
                if (pad) {
                    SDL_RumbleGamepad(pad, 0, 0, 0);
                }
            }
        }
    }

    // Also stop effects on every HapticDevice owned by DeviceManager.
    // These are the devices used by OSCBaseProtocol / OSCProtocol via
    // DispatchHapticCommand, which bypass the OutputMapper target list
    // entirely.  StopAll() clears per-slot effect maps and destroys SDL
    // effect IDs so that the next Play* call starts from a clean state.
    for (const auto& dev : m_DeviceManager.GetDevices()) {
        HapticDevice* haptic = m_DeviceManager.GetHapticDevice(dev.instance_id);
        if (haptic) {
            haptic->StopAll();
        }
    }
}

void OutputMapper::HandleDeviceConnectionChange() {
    if (!m_active_targets) return;

    const auto& devices = m_DeviceManager.GetDevices();
    std::map<std::string, SDL_JoystickID> guidMap;
    for (const auto& dev : devices) {
        if (dev.joystick) {
            guidMap[DeviceManager::GetDeviceGUIDString(dev)] = dev.instance_id;
        }
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
    target.status_message.clear();
    if (target.instance_id == 0) return;

    SDL_Joystick* joystick = GetJoystickByID(target.instance_id, m_DeviceManager);
    if (joystick && (SDL_IsJoystickHaptic(joystick) || SDL_IsGamepad(target.instance_id))) {
        // Try to get existing haptic device from DeviceManager to avoid double-open conflict
        HapticDevice* existingHaptic = m_DeviceManager.GetHapticDevice(target.instance_id);
        if (existingHaptic && existingHaptic->IsReady()) {
            target.haptic_device = existingHaptic->GetHandle();
            target.owns_haptic_device = false;
        } else if (SDL_IsJoystickHaptic(joystick)) {
            target.haptic_device = SDL_OpenHapticFromJoystick(joystick);
            target.owns_haptic_device = true;
        }

        if (target.haptic_device) {
            if (target.owns_haptic_device) {
                if (SDL_HapticRumbleSupported(target.haptic_device)) {
                    if (!SDL_InitHapticRumble(target.haptic_device)) {
                        target.status_message = std::string("Rumble Init Failed: ") + SDL_GetError();
                        SDL_Log("Warning: SDL_InitHapticRumble failed: %s", SDL_GetError());
                    }
                }
                SDL_SetHapticGain(target.haptic_device, 100);
                SDL_SetHapticAutocenter(target.haptic_device, 0);
            }
        } else if (!SDL_IsGamepad(target.instance_id)) {
            target.status_message = std::string("Open Failed: ") + SDL_GetError();
        }
    }
}

void OutputMapper::CloseHapticDevice(HapticTarget& target) {
    if (target.haptic_device) {
        if (target.owns_haptic_device) {
            SDL_CloseHaptic(target.haptic_device);
        }
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

void OutputMapper::QueueCommand(HapticCommand&& cmd) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_CommandQueue.push_back(std::move(cmd));
}

void OutputMapper::QueueRumble(int virtual_id, int slot, float low_freq, float high_freq, int duration_ms) {
    if (low_freq > 0.0f || high_freq > 0.0f) {
        m_lastHapticActivityTime = SDL_GetTicks();
    }
    HapticCommand cmd;
    cmd.type = HapticCommand::RUMBLE;
    cmd.virtual_id = virtual_id;
    cmd.iParams[0] = slot;
    cmd.fParams[0] = low_freq;
    cmd.fParams[1] = high_freq;
    cmd.iParams[1] = duration_ms;
    QueueCommand(std::move(cmd));
}

void OutputMapper::QueueConstantForce(int virtual_id, int slot, float strength, int duration_ms) {
    if (strength != 0.0f) {
        m_lastHapticActivityTime = SDL_GetTicks();
    }
    HapticCommand cmd;
    cmd.type = HapticCommand::CONSTANT;
    cmd.virtual_id = virtual_id;
    cmd.iParams[0] = slot;
    cmd.fParams[0] = strength;
    cmd.iParams[1] = duration_ms;
    QueueCommand(std::move(cmd));
}

void OutputMapper::QueuePeriodic(int virtual_id, int slot, float strength, int period, float magnitude, float offset, int phase, int duration_ms) {
    if (strength > 0.0f && magnitude > 0.0f) {
        m_lastHapticActivityTime = SDL_GetTicks();
    }
    HapticCommand cmd;
    cmd.type = HapticCommand::PERIODIC;
    cmd.virtual_id = virtual_id;
    cmd.iParams[0] = slot;
    cmd.fParams[0] = strength;
    cmd.fParams[1] = magnitude;
    cmd.fParams[2] = offset;
    cmd.iParams[1] = period;
    cmd.iParams[2] = phase;
    cmd.iParams[3] = duration_ms;
    QueueCommand(std::move(cmd));
}

void OutputMapper::QueueCondition(int virtual_id, int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms) {
    m_lastHapticActivityTime = SDL_GetTicks();
    HapticCommand cmd;
    cmd.type = HapticCommand::CONDITION;
    cmd.virtual_id = virtual_id;
    cmd.fParams[0] = right_sat;
    cmd.fParams[1] = left_sat;
    cmd.fParams[2] = right_coeff;
    cmd.fParams[3] = left_coeff;
    cmd.fParams[4] = deadband;
    cmd.fParams[5] = center;
    cmd.iParams[0] = slot;
    cmd.iParams[1] = static_cast<int>(type);  // store as 0–3 index
    cmd.iParams[2] = duration_ms;
    QueueCommand(std::move(cmd));
}

void OutputMapper::QueueSetGain(int virtual_id, int gain) {
    // Not considered an "active" effect for UI purposes
    HapticCommand cmd;
    cmd.type = HapticCommand::GAIN;
    cmd.virtual_id = virtual_id;
    cmd.iParams[0] = gain;
    QueueCommand(std::move(cmd));
}

void OutputMapper::QueueDualSenseTrigger(int virtual_id, const char* trigger, const char* effect_type,
                                         int position, int strength, int end_position,
                                         int amplitude, int frequency, int snap_force,
                                         int first_foot, int second_foot, int period,
                                         int amplitude_a, int amplitude_b) {
    m_lastHapticActivityTime = SDL_GetTicks();
    HapticCommand cmd;
    cmd.type = HapticCommand::DUALSENSE_TRIGGER;
    cmd.virtual_id = virtual_id;
    std::strncpy(cmd.sParams[0], trigger, sizeof(cmd.sParams[0]) - 1);
    std::strncpy(cmd.sParams[1], effect_type, sizeof(cmd.sParams[1]) - 1);
    cmd.iParams[0] = position;
    cmd.iParams[1] = strength;
    cmd.iParams[2] = end_position;
    cmd.iParams[3] = amplitude;
    cmd.iParams[4] = frequency;
    cmd.iParams[5] = snap_force;
    cmd.iParams[6] = first_foot;
    cmd.iParams[7] = second_foot;
    cmd.iParams[8] = period;
    cmd.iParams[9] = (amplitude_a & 0xFF) | ((amplitude_b & 0xFF) << 8);
    QueueCommand(std::move(cmd));
}

// --- Trigger Implementations ---

void OutputMapper::TriggerRumble(int virtual_id, int slot, float low_freq, float high_freq, int duration_ms) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
        if (!target || !target->enable_rumble) continue;

        HapticDevice* hapticDevice = m_DeviceManager.GetHapticDevice(target->instance_id);
        if (hapticDevice) {
            hapticDevice->PlayRumble(slot, low_freq, high_freq, duration_ms);
            continue;
        }

        // Generic fallback for non-managed devices (slot ignored — single motor pair).
        if (target->haptic_device) {
            if (SDL_GetHapticFeatures(target->haptic_device) & SDL_HAPTIC_LEFTRIGHT) {
                SDL_HapticEffect effect;
                SDL_memset(&effect, 0, sizeof(effect));
                effect.type = SDL_HAPTIC_LEFTRIGHT;
                effect.leftright.length = (duration_ms <= 0) ? SDL_HAPTIC_INFINITY : (Uint32)duration_ms;
                effect.leftright.large_magnitude = (Uint16)(std::clamp(low_freq,  0.0f, 1.0f) * 65535.0f);
                effect.leftright.small_magnitude = (Uint16)(std::clamp(high_freq, 0.0f, 1.0f) * 65535.0f);

                bool created = false;
                if (target->rumble_effect_id == -1) {
                    target->rumble_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
                    created = true;
                } else {
                    SDL_UpdateHapticEffect(target->haptic_device, target->rumble_effect_id, &effect);
                }

                if (target->rumble_effect_id != -1) {
                    bool needsRun = created || duration_ms > 0;
                    if (!needsRun) {
                        int status = SDL_GetHapticEffectStatus(target->haptic_device, target->rumble_effect_id);
                        needsRun = (status != 1);
                    }
                    if (needsRun) {
                        SDL_RunHapticEffect(target->haptic_device, target->rumble_effect_id, 1);
                    }
                }
            } else {
                float strength = std::max(low_freq, high_freq);
                SDL_PlayHapticRumble(target->haptic_device, strength, (duration_ms <= 0) ? SDL_HAPTIC_INFINITY : (Uint32)duration_ms);
            }
        }
    }
}

void OutputMapper::TriggerSetGain(int virtual_id, int gain) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
        if (!target || !target->haptic_device) continue;
        SDL_SetHapticGain(target->haptic_device, std::clamp(gain, 0, 100));
    }
}

void OutputMapper::TriggerConstantForce(int virtual_id, int slot, float strength, int duration_ms) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
        if (!target || !target->enable_constant) continue;

        HapticDevice* hapticDevice = m_DeviceManager.GetHapticDevice(target->instance_id);
        if (hapticDevice) {
            hapticDevice->PlayConstant(slot, strength, (duration_ms <= 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
            continue;
        }

        // Generic fallback (slot ignored — single effect slot on unmanaged devices).
        if (!target->haptic_device) continue;

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = 1;
        effect.constant.length = (duration_ms <= 0) ? SDL_HAPTIC_INFINITY : (Uint32)duration_ms;
        effect.constant.level = (Sint16)(std::clamp(strength, -1.0f, 1.0f) * 32767.0f);

        bool created = false;
        if (target->constant_effect_id == -1) {
            target->constant_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
            created = true;
        } else {
            SDL_UpdateHapticEffect(target->haptic_device, target->constant_effect_id, &effect);
        }

        if (target->constant_effect_id != -1) {
            bool needsRun = created || duration_ms > 0;
            if (!needsRun) {
                int status = SDL_GetHapticEffectStatus(target->haptic_device, target->constant_effect_id);
                needsRun = (status != 1);
            }
            if (needsRun) {
                SDL_RunHapticEffect(target->haptic_device, target->constant_effect_id, 1);
            }
        }
    }
}

void OutputMapper::TriggerPeriodic(int virtual_id, int slot, float strength, int period, float magnitude, float offset, int phase, int duration_ms) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
        if (!target || !target->enable_periodic) continue;

        HapticDevice* hapticDevice = m_DeviceManager.GetHapticDevice(target->instance_id);
        if (hapticDevice) {
            hapticDevice->PlayPeriodic(slot, strength, (uint32_t)period, magnitude, offset, (uint32_t)phase, (duration_ms <= 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
            continue;
        }

        // Generic fallback (slot ignored — single effect slot on unmanaged devices).
        if (!target->haptic_device) continue;

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(effect));
        effect.type = SDL_HAPTIC_SINE;
        effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.periodic.direction.dir[0] = 1;
        effect.periodic.length    = (duration_ms <= 0) ? SDL_HAPTIC_INFINITY : (Uint32)duration_ms;
        effect.periodic.period    = (Uint16)period;
        effect.periodic.magnitude = (Sint16)(std::clamp(magnitude, -1.0f, 1.0f) * 32767.0f);
        effect.periodic.offset    = (Sint16)(std::clamp(offset,   -1.0f, 1.0f) * 32767.0f);
        effect.periodic.phase     = (Uint16)std::clamp(phase, 0, 36000);

        if (strength != 1.0f) {
            effect.periodic.magnitude = (Sint16)(effect.periodic.magnitude * strength);
        }

        bool created = false;
        if (target->periodic_effect_id == -1) {
            target->periodic_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
            created = true;
        } else {
            SDL_UpdateHapticEffect(target->haptic_device, target->periodic_effect_id, &effect);
        }

        if (target->periodic_effect_id != -1) {
            bool needsRun = created || duration_ms > 0;
            if (!needsRun) {
                int status = SDL_GetHapticEffectStatus(target->haptic_device, target->periodic_effect_id);
                needsRun = (status != 1);
            }
            if (needsRun) {
                SDL_RunHapticEffect(target->haptic_device, target->periodic_effect_id, 1);
            }
        }
    }
}

void OutputMapper::TriggerCondition(int virtual_id, int slot, HapticConditionType type, float right_sat, float left_sat, float right_coeff, float left_coeff, float deadband, float center, int duration_ms) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
        if (!target || !target->enable_condition) continue;

        // If this target maps to a managed HapticDevice, delegate to it as it
        // has a more advanced, multi-slot implementation for condition effects.
        HapticDevice* hapticDevice = m_DeviceManager.GetHapticDevice(target->instance_id);
        if (hapticDevice) {
            hapticDevice->PlayCondition(slot, type, right_sat, left_sat, right_coeff, left_coeff, deadband, center, (duration_ms < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms);
            continue; // Handled by specialized class
        }

        // Generic fallback for other haptic devices.
        // NOTE: This generic path does not support multiple slots; it uses a single
        // overwritable effect ID, consistent with how Constant and Periodic effects
        // are handled for generic devices. The 'slot' parameter is ignored here.
        if (!target->haptic_device) continue;

        SDL_HapticEffect effect;
        SDL_memset(&effect, 0, sizeof(effect));
        effect.type = ToSDLConditionType(type);  // translate once, here
        effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;
        effect.condition.length = (duration_ms < 0) ? SDL_HAPTIC_INFINITY : (uint32_t)duration_ms;
        effect.condition.right_sat[0] = (Uint16)(std::clamp(right_sat, 0.0f, 1.0f) * 65535.0f);
        effect.condition.left_sat[0]  = (Uint16)(std::clamp(left_sat, 0.0f, 1.0f) * 65535.0f);
        effect.condition.right_coeff[0] = (Sint16)(std::clamp(right_coeff, -1.0f, 1.0f) * 32767.0f);
        effect.condition.left_coeff[0]  = (Sint16)(std::clamp(left_coeff, -1.0f, 1.0f) * 32767.0f);
        effect.condition.deadband[0] = (Uint16)(std::clamp(deadband, 0.0f, 1.0f) * 65535.0f);
        effect.condition.center[0] = (Sint16)(std::clamp(center, -1.0f, 1.0f) * 32767.0f);

        bool created = false;
        if (target->condition_effect_id == -1) {
            target->condition_effect_id = SDL_CreateHapticEffect(target->haptic_device, &effect);
            created = true;
        } else {
            SDL_UpdateHapticEffect(target->haptic_device, target->condition_effect_id, &effect);
        }

        if (target->condition_effect_id != -1) {
            bool needsRun = created || duration_ms >= 0;
            if (!needsRun) {
                int status = SDL_GetHapticEffectStatus(target->haptic_device, target->condition_effect_id);
                needsRun = (status != 1);
            }
            if (needsRun) {
                SDL_RunHapticEffect(target->haptic_device, target->condition_effect_id, 1);
            }
        }
    }
}

void OutputMapper::TriggerDualSenseTrigger(int virtual_id, const char* trigger, const char* effect_type,
                                           int position, int strength, int end_position,
                                           int amplitude, int frequency, int snap_force,
                                           int first_foot, int second_foot, int period,
                                           int amplitude_a, int amplitude_b) {
    std::vector<HapticTarget*> targets;
    GetTargets(virtual_id, targets);
    for (auto* target : targets) {
        if (!target || target->instance_id == 0) continue;

        // Get the haptic device - for DualSense, we need to access GamepadHaptics
        auto* hapticDevice = m_DeviceManager.GetHapticDevice(target->instance_id);
        if (!hapticDevice) continue;

        // Build parameters map
        std::map<std::string, int> params;
        params["position"] = position;
        params["strength"] = strength;
        params["end_position"] = end_position;
        params["start_position"] = position;  // Many effects use start_position instead of position
        params["amplitude"] = amplitude;
        params["frequency"] = frequency;
        params["snap_force"] = snap_force;
        params["first_foot"] = first_foot;
        params["second_foot"] = second_foot;
        params["period"] = period;
        params["amplitude_a"] = amplitude_a;
        params["amplitude_b"] = amplitude_b;

        hapticDevice->PlayDualSenseTrigger(trigger, effect_type, params);
    }
}