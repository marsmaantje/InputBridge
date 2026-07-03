#include "InputMapperUI.h"

#include "InputSignalProcessing.h"
#include "MappingProfileStore.h"
#include "OutputRuntimeUpdater.h"
#include "ProtocolFieldUtils.h"

#include "Devices/DeviceManager.h"
#include "Network/OSCServer.h"
#include "Network/WebSocketServer.h"
#include "Protocols/ProtocolDefinition.h"
#include "Protocols/ProtocolRegistry.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace InputMapping {

namespace {

using SC = InputSource::SensorChannel;

struct SensorEntry {
    SC channel;
    const char* label;
};
const SensorEntry kSensorEntries[] = {
    {SC::GyroX, "Gyro X (pitch)"},
    {SC::GyroY, "Gyro Y (yaw)"},
    {SC::GyroZ, "Gyro Z (roll)"},
    {SC::AccelX, "Accel X (lateral)"},
    {SC::AccelY, "Accel Y (vertical)"},
    {SC::AccelZ, "Accel Z (fore/aft)"},
    {SC::GyroLX, "Gyro L - X  (Left Joy-Con / L half)"},
    {SC::GyroLY, "Gyro L - Y  (Left Joy-Con / L half)"},
    {SC::GyroLZ, "Gyro L - Z  (Left Joy-Con / L half)"},
    {SC::AccelLX, "Accel L - X  (Left Joy-Con / L half)"},
    {SC::AccelLY, "Accel L - Y  (Left Joy-Con / L half)"},
    {SC::AccelLZ, "Accel L - Z  (Left Joy-Con / L half)"},
    {SC::GyroRX, "Gyro R - X  (Right Joy-Con / R half)"},
    {SC::GyroRY, "Gyro R - Y  (Right Joy-Con / R half)"},
    {SC::GyroRZ, "Gyro R - Z  (Right Joy-Con / R half)"},
    {SC::AccelRX, "Accel R - X  (Right Joy-Con / R half)"},
    {SC::AccelRY, "Accel R - Y  (Right Joy-Con / R half)"},
    {SC::AccelRZ, "Accel R - Z  (Right Joy-Con / R half)"},
    {SC::TouchX, "Touch X"},
    {SC::TouchY, "Touch Y"},
    {SC::TouchPressure, "Touch Pressure"},
    {SC::Touch2X, "Touch 2 X"},
    {SC::Touch2Y, "Touch 2 Y"},
    {SC::Touch2Pressure, "Touch 2 Pressure"},
    {SC::LeftStickTouch, "Left Stick Touch"},
    {SC::RightStickTouch, "Right Stick Touch"},
    {SC::LeftGripTouch, "Left Grip Touch"},
    {SC::RightGripTouch, "Right Grip Touch"},
    {SC::BatteryLevel, "Battery Level"},
    {SC::BatteryCharging, "Battery Charging"},
};

const char* SensorChannelName(SC ch) {
    for (const auto& e : kSensorEntries)
        if (e.channel == ch) return e.label;
    return nullptr;
}

} // namespace

InputMapperUI::InputMapperUI(const DeviceManager& deviceManager, MappingProfileStore& store,
                              InputBindingListener& listener, OutputRuntimeUpdater& updater)
    : m_DeviceManager(deviceManager), m_Store(store), m_Listener(listener), m_Updater(updater) {}

void InputMapperUI::DrawContent() {
    ImGui::Begin("Input Mapper");
    DrawProfileSelector(); // calls HandleDeviceConnectionChange + listener update
    DrawMappingContent();
    ImGui::End();
}

// Draws a "pick a protocol definition" combo for one
// transport/direction and reports whether the selection changed.
bool InputMapperUI::DrawProtocolDefinitionCombo(const char* label, std::string& currentId,
                                                 ProtocolTransport transport, ProtocolDirection dir, float width) {
    bool changed = false;
    std::string preview = "None";
    if (!currentId.empty()) {
        if (auto* def = ProtocolRegistry::GetInstance().FindById(currentId)) preview = def->name;
        else preview = "Unknown (" + currentId + ")";
    }
    if (width > 0.f) ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("None", currentId.empty())) {
            currentId = "";
            changed = true;
        }
        for (const auto& def : ProtocolRegistry::GetInstance().GetDefinitions()) {
            if (def.transport != transport || def.direction != dir) continue;
            if (ImGui::Selectable(def.name.c_str(), def.id == currentId)) {
                currentId = def.id;
                changed = true;
                m_Store.UpdateActiveProtocols();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// The OSC / WebSocket tab switcher plus "(Active)" / "[Running]"/"[Stopped]" indicators.
void InputMapperUI::DrawProtocolViewSelector(const char* comboId, bool oscActive, bool wsActive, bool oscRunning,
                                              bool wsRunning, const char* tooltip, bool& changed) {
    ImGui::SetNextItemWidth(120);
    int oldView = m_Store.SelectedProtocolView();
    if (ImGui::BeginCombo(comboId, oldView == 0 ? "OSC" : "WebSocket")) {
        if (ImGui::Selectable("OSC", m_Store.SelectedProtocolView() == 0)) m_Store.SetSelectedProtocolView(0);
        if (oscActive) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "(Active)");
        }
        if (ImGui::Selectable("WebSocket", m_Store.SelectedProtocolView() == 1)) m_Store.SetSelectedProtocolView(1);
        if (wsActive) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "(Active)");
        }
        ImGui::EndCombo();
    }
    if (oldView != m_Store.SelectedProtocolView()) {
        if (auto* profile = m_Store.CurrentProfile()) {
            profile->selectedProtocolView = m_Store.SelectedProtocolView();
            changed = true;
        }
    }

    ImGui::SameLine();
    bool isRunning = (m_Store.SelectedProtocolView() == 0) ? oscRunning : wsRunning;
    if (isRunning) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[Running]");
    else ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[Stopped]");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("%s", tooltip);
}

void InputMapperUI::DrawProfileSelector() {
    m_Store.HandleDeviceConnectionChange();
    if (auto* profile = m_Store.CurrentProfile()) {
        if (m_Listener.Update(*profile, m_DeviceManager)) m_Store.SaveCurrentProfile();
    } else if (m_Listener.IsActive()) {
        m_Listener.CancelListening();
    }

    // Profile management
    ImGui::Text("Mapping Profiles");
    ImGui::Separator();

    auto& profiles = m_Store.Profiles();
    int selectedIndex = m_Store.SelectedIndex();
    int newIndex = selectedIndex;
    const char* cur = selectedIndex != -1 ? profiles[selectedIndex].name.c_str() : "None";
    if (ImGui::BeginCombo("Active Profile", cur)) {
        if (ImGui::Selectable("None", selectedIndex == -1)) newIndex = -2; // sentinel to trigger swap
        for (int i = 0; i < (int)profiles.size(); ++i) {
            if (ImGui::Selectable(profiles[i].name.c_str(), selectedIndex == i)) newIndex = i;
            if (selectedIndex == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    // Route all activation through ActivateProfile() so the swap is always atomic:
    // haptic targets, protocol IDs, and server settings all change together.
    if (newIndex == -2) m_Store.ActivateProfile(-1);
    else if (newIndex != selectedIndex && newIndex >= 0) m_Store.ActivateProfile(newIndex);

    selectedIndex = m_Store.SelectedIndex();
    float avail = ImGui::GetContentRegionAvail().x;
    float spc = ImGui::GetStyle().ItemSpacing.x;
    float cw = ImGui::CalcTextSize("Create New").x + ImGui::GetStyle().FramePadding.x * 2;
    float renw = ImGui::CalcTextSize("Rename").x + ImGui::GetStyle().FramePadding.x * 2;
    float dw = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2;
    float iw = avail - cw - spc - (selectedIndex != -1 ? dw + spc + renw + spc : 0);
    ImGui::SetNextItemWidth(std::max(1.f, iw));
    ImGui::InputTextWithHint("##npn", "New Profile Name", m_NewProfileName, sizeof(m_NewProfileName));
    ImGui::SameLine();
    if (ImGui::Button("Create New") && strlen(m_NewProfileName) > 0) {
        m_Store.CreateProfile(m_NewProfileName);
        m_NewProfileName[0] = '\0';
    }

    if (selectedIndex == -1) return;

    ImGui::SameLine();
    if (ImGui::Button("Rename")) {
        std::strncpy(m_RenameProfileName, profiles[selectedIndex].name.c_str(), sizeof(m_RenameProfileName));
        m_RenameProfileName[sizeof(m_RenameProfileName) - 1] = '\0';
        ImGui::OpenPopup("Rename Profile");
    }
    if (ImGui::BeginPopupModal("Rename Profile", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("New Name", m_RenameProfileName, sizeof(m_RenameProfileName));
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            m_Store.RenameProfile(selectedIndex, m_RenameProfileName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete Profile?");
    if (ImGui::BeginPopupModal("Delete Profile?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete '%s'?", profiles[selectedIndex].name.c_str());
        ImGui::Separator();
        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            m_Store.DeleteProfile(selectedIndex);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void InputMapperUI::DrawOutputProtocolSelector() {
    auto* profile = m_Store.CurrentProfile();
    if (!profile) return;
    bool changed = false;

    if (m_Store.SelectedProtocolView() == 0) {
        changed |= DrawProtocolDefinitionCombo("OSC Output##out", profile->oscOutputProtocolId,
                                                ProtocolTransport::OSC, ProtocolDirection::Output, 200.f);
    } else {
#ifdef ENABLE_WEBSOCKETS
        changed |= DrawProtocolDefinitionCombo("WS Output##out", profile->wsOutputProtocolId,
                                                ProtocolTransport::WebSocket, ProtocolDirection::Output, 200.f);
#else
        ImGui::TextDisabled("WebSockets disabled.");
#endif
    }
    if (changed) m_Store.SaveProfile(*profile);
}

void InputMapperUI::DrawInputProtocolSelector() {
    auto* profile = m_Store.CurrentProfile();
    if (!profile) return;
    bool changed = false;

    // ── Active Protocol Selection ─────────────────────────────────────────────
    bool oscActive = !OSCServer::GetInstance().GetInputDefinitionId().empty();
    bool oscRunning = OSCServer::GetInstance().IsRunning();
#ifdef ENABLE_WEBSOCKETS
    bool wsActive = !WebSocketServer::GetInstance().GetInputDefinitionId().empty();
    bool wsRunning = WebSocketServer::GetInstance().IsRunning();
#else
    bool wsActive = false;
    bool wsRunning = false;
#endif
    DrawProtocolViewSelector("##protoview_in", oscActive, wsActive, oscRunning, wsRunning,
                             "Select which protocol definition to use for incoming data.\nOnly one definition "
                             "drives the output mapping at a time.",
                             changed);

    if (m_Store.SelectedProtocolView() == 0) {
        changed |= DrawProtocolDefinitionCombo("OSC Input", profile->oscInputProtocolId, ProtocolTransport::OSC,
                                                ProtocolDirection::Input);
    } else {
#ifdef ENABLE_WEBSOCKETS
        changed |= DrawProtocolDefinitionCombo("WebSocket Input", profile->wsInputProtocolId,
                                                ProtocolTransport::WebSocket, ProtocolDirection::Input);
#else
        ImGui::TextDisabled("WebSockets are disabled in this build.");
#endif
    }

    if (changed) m_Store.SaveProfile(*profile);
}

// Returns the [lo, hi] output bounds for a source's configured range, used
// only to scale the live value bar display — the actual remap math lives in
// InputSignalProcessing.cpp's ApplyOutputRange and must be kept in sync.
namespace {
std::pair<float, float> GetOutputRangeBounds(const InputSource& src) {
    switch (src.outputRange) {
        case 1: return {0.f, 1.f};                                   // 0..1
        case 2: return {-1.f, 0.f};                                  // -1..0
        case 3: return {0.f, 1.f};                                   // +half
        case 4: return {0.f, 1.f};                                   // -half
        case 5: return {std::min(src.customRangeMin, src.customRangeMax),
                         std::max(src.customRangeMin, src.customRangeMax)}; // custom
        default: return {-1.f, 1.f};                                 // -1..1
    }
}
} // namespace

// Draws a dual-direction live value bar (centred at zero) for an analog
// source. Shows the post-deadzone, post-range processed output value.
void InputMapperUI::DrawAnalogLiveBar(const InputSource& src) {
    if (src.instance_id == 0 || (src.axisIndex == -1 && src.sensorChannel == SC::None)) return;

    float val = ReadInputSourceValue(src, m_DeviceManager);

    float barW = ImGui::GetContentRegionAvail().x;
    float bH = ImGui::GetFrameHeight() * 0.6f;
    ImVec2 bPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background track
    dl->AddRectFilled(bPos, ImVec2(bPos.x + barW, bPos.y + bH), IM_COL32(50, 50, 50, 180), 3.f);

    // Center line
    float cx = bPos.x + barW * 0.5f;
    dl->AddLine(ImVec2(cx, bPos.y), ImVec2(cx, bPos.y + bH), IM_COL32(120, 120, 120, 180), 1.f);

    // Fill from centre to current value. `val` is scaled into [0, 1] using
    // the source's configured output bounds (not a hardcoded -1..1) so
    // custom ranges — including ones well outside -1..1 — still render
    // sensibly instead of clipping to the bar's edges.
    auto [lo, hi] = GetOutputRangeBounds(src);
    float frac = (hi > lo) ? std::clamp((val - lo) / (hi - lo), 0.f, 1.f) : 0.5f;
    float fillX = bPos.x + barW * frac;
    ImU32 fillCol = IM_COL32(60, 180, 100, 200);
    if (fillX > cx) dl->AddRectFilled(ImVec2(cx, bPos.y + 1), ImVec2(fillX, bPos.y + bH - 1), fillCol, 2.f);
    else dl->AddRectFilled(ImVec2(fillX, bPos.y + 1), ImVec2(cx, bPos.y + bH - 1), fillCol, 2.f);

    // Deadzone boundary lines: two symmetric orange markers at ±deadzone
    // mapped from [-1..1] space onto the bar width.
    if (src.deadzone > 0.f) {
        ImU32 dzCol = IM_COL32(255, 180, 50, 230);
        float dzFracPos = std::clamp((src.deadzone + 1.f) * 0.5f, 0.f, 1.f);
        float dzFracNeg = std::clamp((-src.deadzone + 1.f) * 0.5f, 0.f, 1.f);
        float dzXPos = bPos.x + barW * dzFracPos;
        float dzXNeg = bPos.x + barW * dzFracNeg;
        dl->AddLine(ImVec2(dzXPos, bPos.y), ImVec2(dzXPos, bPos.y + bH), dzCol, 2.f);
        dl->AddLine(ImVec2(dzXNeg, bPos.y), ImVec2(dzXNeg, bPos.y + bH), dzCol, 2.f);
    }

    // Advance cursor and show numeric value beside the bar
    ImGui::Dummy(ImVec2(barW, bH));
    ImGui::SameLine();
    ImGui::Text("%.3f", val);
}

void InputMapperUI::DrawAxisCombo(const std::string& id, InputSource& src, const char* comboId, float colW,
                                   bool& changed, bool showBind) {
    // Build preview string: sensor name takes priority over axis index.
    std::string preview = "None";
    if (src.sensorChannel != SC::None) {
        for (const auto& d : m_DeviceManager.GetDevices())
            if (d.instance_id == src.instance_id) {
                const char* sn = SensorChannelName(src.sensorChannel);
                preview = d.name + " - " + (sn ? sn : "Sensor");
                break;
            }
    } else if (src.instance_id != 0) {
        for (const auto& d : m_DeviceManager.GetDevices())
            if (d.instance_id == src.instance_id) {
                preview = d.name + " - Axis " + std::to_string(src.axisIndex);
                break;
            }
    }

    ImGuiStyle& style = ImGui::GetStyle();
    float sp = style.ItemSpacing.x;
    float bindW = ImGui::CalcTextSize("Bind").x + style.FramePadding.x * 2;

    float dw = 80.f, rw = 0.f;
    bool hasSrc = (src.axisIndex != -1) || (src.sensorChannel != SC::None);
    if (hasSrc) {
        dw = 180.f;
        rw = 80.f;
    }

    ImGui::SetNextItemWidth(std::max(1.0f, colW - sp - (showBind ? bindW + sp : 0.f)));

    if (ImGui::BeginCombo(comboId, preview.c_str())) {
        if (ImGui::Selectable("None", !hasSrc)) {
            src = {};
            changed = true;
        }

        // ── Regular device axes ─────────────────────────────────────────
        for (const auto& dev : m_DeviceManager.GetDevices())
            for (int i = 0; i < dev.num_axes; ++i) {
                std::string lbl = dev.name + " - Axis " + std::to_string(i);
                bool sel = src.sensorChannel == SC::None && src.instance_id == dev.instance_id && src.axisIndex == i;
                if (ImGui::Selectable(lbl.c_str(), sel)) {
                    src.deviceGuid = DeviceManager::GetDeviceGUIDString(dev);
                    src.instance_id = dev.instance_id;
                    src.axisIndex = i;
                    src.sensorChannel = SC::None;
                    changed = true;
                }
            }

        // ── Sensor channels (DualSense / Steam Controller) ──────────────
        for (const auto& dev : m_DeviceManager.GetDevices()) {
            if (!dev.gamepad) continue;
            bool hasGyro = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO);
            bool hasAccel = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL);
            bool hasTouch = SDL_GetNumGamepadTouchpads(dev.gamepad) > 0;
            bool hasGyroLR = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_L) ||
                              SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_R);
            bool hasAccelLR = SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_L) ||
                               SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_R);
            bool hasBattery = (dev.battery_state != SDL_POWERSTATE_UNKNOWN || dev.battery_percent >= 0);
            if (!hasGyro && !hasAccel && !hasGyroLR && !hasAccelLR && !hasTouch && !hasBattery) continue;

            ImGui::Separator();
            ImGui::TextDisabled("%s  [Sensors/Battery]", dev.name.c_str());

            for (const auto& entry : kSensorEntries) {
                // Cap-sense (stick/grip touch) and BatteryCharging are boolean
                // signals — they belong in the digital combo only, not here.
                bool isCap = (entry.channel == SC::LeftStickTouch || entry.channel == SC::RightStickTouch ||
                              entry.channel == SC::LeftGripTouch || entry.channel == SC::RightGripTouch);
                if (isCap || entry.channel == SC::BatteryCharging) continue;

                bool isGyro = (entry.channel >= SC::GyroX && entry.channel <= SC::GyroZ);
                bool isAccel = (entry.channel >= SC::AccelX && entry.channel <= SC::AccelZ);
                bool isGyroL = (entry.channel >= SC::GyroLX && entry.channel <= SC::GyroLZ);
                bool isAccelL = (entry.channel >= SC::AccelLX && entry.channel <= SC::AccelLZ);
                bool isGyroR = (entry.channel >= SC::GyroRX && entry.channel <= SC::GyroRZ);
                bool isAccelR = (entry.channel >= SC::AccelRX && entry.channel <= SC::AccelRZ);
                bool isTouch = (entry.channel >= SC::TouchX && entry.channel <= SC::Touch2Pressure);
                bool isBattery = (entry.channel == SC::BatteryLevel);

                // Hide combined sensors if split (L/R) sensors are available, to avoid redundancy.
                if (isGyro && (hasGyroLR || !hasGyro)) continue;
                if (isAccel && (hasAccelLR || !hasAccel)) continue;
                if (isGyroL && !SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_L)) continue;
                if (isAccelL && !SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_L)) continue;
                if (isGyroR && !SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_GYRO_R)) continue;
                if (isAccelR && !SDL_GamepadHasSensor(dev.gamepad, SDL_SENSOR_ACCEL_R)) continue;
                if (isTouch && !hasTouch) continue;
                if (isBattery && !hasBattery) continue;

                std::string lbl = std::string("  ") + entry.label;
                bool sel = src.instance_id == dev.instance_id && src.sensorChannel == entry.channel;
                if (ImGui::Selectable(lbl.c_str(), sel)) {
                    src.deviceGuid = DeviceManager::GetDeviceGUIDString(dev);
                    src.instance_id = dev.instance_id;
                    src.axisIndex = -1;
                    src.sensorChannel = entry.channel;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    bool isListening = m_Listener.IsListeningFor(InputBindingListener::Type::Axis, id);
    if (showBind) {
        if (isListening) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
            if (ImGui::Button("Waiting...")) m_Listener.CancelListening();
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Waiting for input... Click to cancel.");
        } else {
            if (ImGui::Button("Bind")) m_Listener.StartListening(InputBindingListener::Type::Axis, id, -1, m_DeviceManager);
            ImGui::SetItemTooltip("Click to bind an axis.");
        }
    }

    // Live value bar — drawn after the device/axis dropdown and Bind button,
    // before the Inv/DZ/Range options row.
    DrawAnalogLiveBar(src);

    if (hasSrc) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("X")) {
            src = {};
            changed = true;
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Clear");
        ImGui::SameLine();
        if (ImGui::Checkbox("Inv", &src.invert)) changed = true;
        ImGui::SetItemTooltip("Invert");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(dw);
        if (ImGui::SliderFloat("DZ", &src.deadzone, 0.f, 1.0f, "%.3f")) changed = true;
        ImGui::SetItemTooltip("Deadzone: axis input below this absolute value is zeroed out.\nOrange lines on the "
                               "value bar show the deadzone boundary on each side of centre.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(rw);
        const char* ranges[] = {"-1..1", "0..1", "-1..0", "+half (0..1)", "-half (0..1)", "Custom..."};
        if (ImGui::Combo("Range", &src.outputRange, ranges, IM_ARRAYSIZE(ranges))) changed = true;
        ImGui::SetItemTooltip("Output range: remaps the processed -1..1 signal onto this span.\nChoose \"Custom...\" "
                               "to enter your own minimum and maximum output values.");

        if (src.outputRange == 5) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(dw * 0.5f);
            bool minChanged = ImGui::DragFloat("##rangeMin", &src.customRangeMin, 0.01f, -1000.f, 1000.f, "Min %.3f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(dw * 0.5f);
            bool maxChanged = ImGui::DragFloat("##rangeMax", &src.customRangeMax, 0.01f, -1000.f, 1000.f, "Max %.3f");
            if (minChanged || maxChanged) {
                // Keep min <= max so the linear remap in ApplyOutputRange
                // never inverts unexpectedly from a stray drag.
                if (src.customRangeMin > src.customRangeMax) {
                    if (minChanged) src.customRangeMax = src.customRangeMin;
                    else src.customRangeMin = src.customRangeMax;
                }
                changed = true;
            }
            ImGui::SetItemTooltip("Custom output range bounds. The processed input always spans this range "
                                   "linearly, with the low end at minimum input and the high end at maximum "
                                   "input.");
        }
    }
}

void InputMapperUI::DrawButtonInputCombo(const char* comboId, SDL_JoystickID instance_id, int& button_index,
                                          int* hat_index, int* hat_mask, InputSource::SensorChannel* sensor_channel,
                                          bool& changed) {
    std::string preview = "None";
    if (instance_id != 0) {
        if (button_index != -1) {
            preview = "Button " + std::to_string(button_index);
        } else if (hat_index && *hat_index != -1 && hat_mask) {
            preview = "Hat " + std::to_string(*hat_index);
            if (*hat_mask & SDL_HAT_UP) preview += " Up";
            if (*hat_mask & SDL_HAT_DOWN) preview += " Down";
            if (*hat_mask & SDL_HAT_LEFT) preview += " Left";
            if (*hat_mask & SDL_HAT_RIGHT) preview += " Right";
            if (*hat_mask == SDL_HAT_CENTERED) preview += " Centered";
        } else if (sensor_channel && *sensor_channel != SC::None) {
            const char* sn = SensorChannelName(*sensor_channel);
            preview = (sn ? sn : "Sensor");
        }
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(comboId, preview.c_str())) {
        if (ImGui::Selectable("None", button_index == -1 && (!hat_index || *hat_index == -1) &&
                                           (!sensor_channel || *sensor_channel == SC::None))) {
            button_index = -1;
            if (hat_index) *hat_index = -1;
            if (hat_mask) *hat_mask = 0;
            if (sensor_channel) *sensor_channel = SC::None;
            changed = true;
        }
        if (instance_id != 0) {
            const DeviceState* dev = nullptr;
            for (const auto& d : m_DeviceManager.GetDevices())
                if (d.instance_id == instance_id) { dev = &d; break; }
            if (dev && dev->joystick) {
                for (int i = 0; i < dev->num_buttons; ++i) {
                    bool sel = (button_index == i);
                    if (ImGui::Selectable(("Button " + std::to_string(i)).c_str(), sel)) {
                        button_index = i;
                        if (hat_index) *hat_index = -1;
                        if (hat_mask) *hat_mask = 0;
                        if (sensor_channel) *sensor_channel = SC::None;
                        changed = true;
                    }
                }
                if (hat_index && hat_mask) {
                    for (int i = 0; i < dev->num_hats; ++i) {
                        struct {
                            int mask;
                            const char* name;
                        } dirs[] = {{SDL_HAT_UP, "Up"}, {SDL_HAT_DOWN, "Down"}, {SDL_HAT_LEFT, "Left"}, {SDL_HAT_RIGHT, "Right"}};
                        for (auto& d : dirs) {
                            bool sel = (*hat_index == i && *hat_mask == d.mask);
                            if (ImGui::Selectable(("Hat " + std::to_string(i) + " " + d.name).c_str(), sel)) {
                                button_index = -1;
                                *hat_index = i;
                                *hat_mask = d.mask;
                                if (sensor_channel) *sensor_channel = SC::None;
                                changed = true;
                            }
                        }
                    }
                }
                if (sensor_channel && dev->gamepad) {
                    ImGui::Separator();
                    for (const auto& entry : kSensorEntries) {
                        // Only expose boolean sensor channels as digital inputs:
                        // cap-sense touch pads and battery charging state. Gyro,
                        // accel, touchpad position/pressure, and battery level
                        // are all continuous/analog and must not appear here.
                        bool isCap = (entry.channel == SC::LeftStickTouch || entry.channel == SC::RightStickTouch ||
                                      entry.channel == SC::LeftGripTouch || entry.channel == SC::RightGripTouch);
                        bool isCharge = (entry.channel == SC::BatteryCharging);
                        if (!isCap && !isCharge) continue;

                        SDL_GamepadCapSenseType ct = SDL_GAMEPAD_CAPSENSE_LEFT_STICK;
                        if (entry.channel == SC::RightStickTouch) ct = SDL_GAMEPAD_CAPSENSE_RIGHT_STICK;
                        else if (entry.channel == SC::LeftGripTouch) ct = SDL_GAMEPAD_CAPSENSE_LEFT_GRIP;
                        else if (entry.channel == SC::RightGripTouch) ct = SDL_GAMEPAD_CAPSENSE_RIGHT_GRIP;
                        if (isCap && !SDL_GamepadHasCapSense(dev->gamepad, ct)) continue;

                        bool sel = (*sensor_channel == entry.channel);
                        if (ImGui::Selectable(entry.label, sel)) {
                            button_index = -1;
                            if (hat_index) *hat_index = -1;
                            if (hat_mask) *hat_mask = 0;
                            *sensor_channel = entry.channel;
                            changed = true;
                        }
                    }
                }
            }
        } else {
            ImGui::TextDisabled("Select a device first");
        }
        ImGui::EndCombo();
    }
}

namespace {
// Garbage-collects unused toggle states: if a field no longer has any
// mappings in a state-managing mode (Toggle/SetOn/SetOff), its persistent
// state entry is removed to keep the profile clean and prevent ghosting.
void GarbageCollectToggleStates(MappingProfile& profile) {
    std::unordered_set<std::string_view> activeFields;
    for (const auto& dm : profile.digitalMappings)
        if (!dm.target_field_id.empty() && dm.mode != ButtonToDigitalMapping::Mode::Momentary)
            activeFields.insert(dm.target_field_id);
    for (const auto& am : profile.analogToDigitalMappings)
        if (!am.target_field_id.empty() && am.mode != AnalogToDigitalMapping::Mode::Momentary)
            activeFields.insert(am.target_field_id);

    for (auto it = profile.digitalToggleStates.begin(); it != profile.digitalToggleStates.end();) {
        if (activeFields.find(it->first) == activeFields.end()) it = profile.digitalToggleStates.erase(it);
        else ++it;
    }
}
} // namespace

void InputMapperUI::DrawMappingContent() {
    ImGui::Separator();
    auto* profilePtr = m_Store.CurrentProfile();
    if (!profilePtr) {
        ImGui::TextDisabled("Select or create a profile above.");
        return;
    }
    MappingProfile& profile = *profilePtr;
    bool changed = false;

    // ── Active Protocol Selection (top of page) ───────────────────────────────
    bool oscActive = !OSCServer::GetInstance().GetOutputDefinitionId().empty();
    bool oscRunning = OSCServer::GetInstance().IsRunning();
#ifdef ENABLE_WEBSOCKETS
    bool wsActive = !WebSocketServer::GetInstance().GetOutputDefinitionId().empty();
    bool wsRunning = WebSocketServer::GetInstance().IsRunning();
#else
    bool wsActive = false;
    bool wsRunning = false;
#endif
    DrawProtocolViewSelector("##protoview", oscActive, wsActive, oscRunning, wsRunning,
                             "Select which protocol definition to use for mapping and preview.\nOnly one "
                             "definition drives the input mapping at a time.",
                             changed);

    if (m_Store.SelectedProtocolView() == 0) {
        changed |= DrawProtocolDefinitionCombo("OSC Output", profile.oscOutputProtocolId, ProtocolTransport::OSC,
                                                ProtocolDirection::Output);
    } else {
#ifdef ENABLE_WEBSOCKETS
        changed |= DrawProtocolDefinitionCombo("WebSocket Output", profile.wsOutputProtocolId,
                                                ProtocolTransport::WebSocket, ProtocolDirection::Output);
#else
        ImGui::TextDisabled("WebSockets are disabled in this build.");
#endif
    }

    ImGui::Separator();
    ImGui::Text("'%s' Mappings", profile.name.c_str());

    const ProtocolDefinition* outDef = m_Store.GetActiveOutputDefinition();

    ImGui::Spacing();
    DrawAnalogOutputSection(profile, outDef, changed);
    DrawDigitalOutputSection(profile, outDef, changed);
    DrawButtonToAnalogSection(profile, outDef, changed);
    DrawChannelMixSection(profile, outDef, changed);
    DrawAnalogToDigitalSection(profile, outDef, changed);

    ImGui::Spacing();
    ImGui::Separator();

    if (changed) {
        GarbageCollectToggleStates(profile);
        m_Store.SaveProfile(profile);
    }

    ImGui::Separator();
    ImGui::Text("Output Preview:");
    ImGui::TextWrapped(
        "%s", m_Updater.GetOutputPreview(profile, outDef, m_Store.SelectedProtocolView(), m_DeviceManager, kGenericOutputs)
                  .c_str());
}

void InputMapperUI::DrawAnalogOutputSection(MappingProfile& profile, const ProtocolDefinition* outDef,
                                             bool& changed) {
    if (outDef) {
        auto analogFields = GetEnabledFields(*outDef, FieldType::AnalogAxis);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.f, 1.f), "Analog Output Channels  (%s)", outDef->name.c_str());
        if (analogFields.empty()) {
            ImGui::TextDisabled("No enabled analog fields in this protocol.");
            return;
        }
        if (ImGui::BeginTable("t_analog", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 170.f);
            ImGui::TableSetupColumn("Device Axis", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            std::string currentCategory;
            for (auto& [pf, fd] : analogFields) {
                // Emit a full-width category header row whenever the category changes.
                if (fd->category != currentCategory) {
                    currentCategory = fd->category;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                          ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.f)));
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                          ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.f)));
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.f), "%s", currentCategory.c_str());
                    // Leave column 1 blank — the category label spans visually via the row colour.
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushTextWrapPos(0.0f); // wrap at the right edge of this column
                ImGui::Text("%s", fd->label.c_str());
                ImGui::PopTextWrapPos();
                ImGui::SetItemTooltip("OSC: %s\nWS:  %s", pf->oscPath.c_str(), pf->wsKey.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(("a_" + pf->fieldId).c_str());
                DrawAxisCombo(pf->fieldId, profile.outputToInput[pf->fieldId], "##ax",
                              ImGui::GetContentRegionAvail().x, changed);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        return;
    }

    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.f), "Analog Output Channels  (legacy – no protocol selected)");
    if (ImGui::BeginTable("t_legacy", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, 120.f);
        ImGui::TableSetupColumn("Device Axis", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const auto& name : kGenericOutputs) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::Text("%s", name.c_str());
            ImGui::PopTextWrapPos();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(name.c_str());
            DrawAxisCombo(name, profile.outputToInput[name], "##ax", ImGui::GetContentRegionAvail().x, changed);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void InputMapperUI::DrawDigitalOutputSection(MappingProfile& profile, const ProtocolDefinition* outDef,
                                              bool& changed) {
    if (!outDef) return;
    auto digitalFields = GetEnabledFields(*outDef, FieldType::DigitalButton);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 1.f, 0.6f, 1.f), "Digital Output Channels  (%s)", outDef->name.c_str());

    if (digitalFields.empty()) {
        ImGui::TextDisabled("No enabled digital fields in this protocol.");
        return;
    }

    ImGui::TextWrapped("Bind device buttons to digital (0/1) output fields.");
    if (ImGui::Button("Add Digital Mapping")) {
        profile.digitalMappings.push_back({});
        changed = true;
    }

    int toDelete = -1;
    if (ImGui::BeginTable("t_digital", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableSetupColumn("Digital Field", ImGuiTableColumnFlags_WidthFixed, 150.f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)profile.digitalMappings.size(); ++i) {
            auto& dm = profile.digitalMappings[i];
            bool rc = false;
            ImGui::PushID(5000 + i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            std::string dname = "None";
            for (const auto& d : m_DeviceManager.GetDevices())
                if (d.instance_id == dm.instance_id) { dname = d.name; break; }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##dd", dname.c_str())) {
                if (ImGui::Selectable("None", dm.instance_id == 0)) {
                    dm.device_guid = "";
                    dm.instance_id = 0;
                    rc = true;
                }
                for (const auto& d : m_DeviceManager.GetDevices())
                    if (ImGui::Selectable(d.name.c_str(), dm.instance_id == d.instance_id)) {
                        dm.device_guid = DeviceManager::GetDeviceGUIDString(d);
                        dm.instance_id = d.instance_id;
                        rc = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::TableSetColumnIndex(1);
            DrawButtonInputCombo("##db", dm.instance_id, dm.button_index, &dm.hat_index, &dm.hat_mask,
                                 &dm.sensor_channel, rc);

            ImGui::TableSetColumnIndex(2);
            std::string flabel = dm.target_field_id.empty() ? "None" : dm.target_field_id;
            for (auto& [pf2, fd2] : digitalFields)
                if (pf2->fieldId == dm.target_field_id) { flabel = fd2->label; break; }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##df", flabel.c_str())) {
                if (ImGui::Selectable("None", dm.target_field_id.empty())) {
                    dm.target_field_id = "";
                    rc = true;
                }
                for (auto& [pf2, fd2] : digitalFields) {
                    bool s = dm.target_field_id == pf2->fieldId;
                    if (ImGui::Selectable(fd2->label.c_str(), s)) {
                        dm.target_field_id = pf2->fieldId;
                        rc = true;
                    }
                    if (s) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TableSetColumnIndex(3);
            const char* modes[] = {"Momentary", "Toggle", "Set On", "Set Off"};
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##mode", (int*)&dm.mode, modes, IM_ARRAYSIZE(modes))) changed = true;

            ImGui::TableSetColumnIndex(4);
            bool isListening = m_Listener.IsListeningFor(InputBindingListener::Type::Digital, "digital", i);
            if (isListening) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                if (ImGui::Button("Waiting...")) m_Listener.CancelListening();
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("Waiting for input... Click to cancel.");
            } else {
                if (ImGui::Button("Bind"))
                    m_Listener.StartListening(InputBindingListener::Type::Digital, "digital", i, m_DeviceManager);
                ImGui::SetItemTooltip("Click to bind a button.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) toDelete = i;
            if (rc) changed = true;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (toDelete != -1) {
        profile.digitalMappings.erase(profile.digitalMappings.begin() + toDelete);
        changed = true;
    }
}

void InputMapperUI::DrawButtonToAnalogSection(MappingProfile& profile, const ProtocolDefinition* outDef,
                                               bool& changed) {
    ImGui::Spacing();
    ImGui::Separator();
    if (outDef)
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.f, 1.f), "Button to Analog Mappings  (%s)", outDef->name.c_str());
    else
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.f), "Button to Analog Mappings  (legacy - no protocol selected)");
    ImGui::TextWrapped("Override an analog output with a fixed value when a button is held.");
    if (ImGui::Button("Add Mapping")) {
        profile.buttonMappings.push_back({});
        changed = true;
    }

    int bToDelete = -1;
    if (ImGui::BeginTable("t_btn_analog", 6,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 150.f);
        ImGui::TableSetupColumn("On Val", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Off Val", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)profile.buttonMappings.size(); ++i) {
            auto& bm = profile.buttonMappings[i];
            bool rc = false;
            ImGui::PushID(1000 + i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            std::string dname = "None";
            for (const auto& d : m_DeviceManager.GetDevices())
                if (d.instance_id == bm.instance_id) { dname = d.name; break; }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##bd", dname.c_str())) {
                if (ImGui::Selectable("None", bm.instance_id == 0)) {
                    bm.device_guid = "";
                    bm.instance_id = 0;
                    rc = true;
                }
                for (const auto& d : m_DeviceManager.GetDevices())
                    if (ImGui::Selectable(d.name.c_str(), bm.instance_id == d.instance_id)) {
                        bm.device_guid = DeviceManager::GetDeviceGUIDString(d);
                        bm.instance_id = d.instance_id;
                        rc = true;
                    }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(1);
            DrawButtonInputCombo("##bb", bm.instance_id, bm.button_index, &bm.hat_index, &bm.hat_mask,
                                 &bm.sensor_channel, rc);

            ImGui::TableSetColumnIndex(2);
            std::string tlabel = bm.target_output_name.empty() ? "None" : bm.target_output_name;
            if (outDef)
                for (auto& [pf2, fd2] : GetEnabledFields(*outDef, FieldType::AnalogAxis))
                    if (pf2->fieldId == bm.target_output_name) { tlabel = fd2->label; break; }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##bt", tlabel.c_str())) {
                if (ImGui::Selectable("None", bm.target_output_name.empty())) {
                    bm.target_output_name = "";
                    rc = true;
                }
                if (outDef) {
                    for (auto& [pf2, fd2] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
                        bool s = bm.target_output_name == pf2->fieldId;
                        if (ImGui::Selectable(fd2->label.c_str(), s)) {
                            bm.target_output_name = pf2->fieldId;
                            rc = true;
                        }
                    }
                } else {
                    for (const auto& name : kGenericOutputs)
                        if (ImGui::Selectable(name.c_str(), bm.target_output_name == name)) {
                            bm.target_output_name = name;
                            rc = true;
                        }
                }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##bon", &bm.on_value, 0.01f, -1.f, 1.f)) rc = true;
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##boff", &bm.off_value, 0.01f, -1.f, 1.f)) rc = true;
            ImGui::TableSetColumnIndex(5);
            bool isListening = m_Listener.IsListeningFor(InputBindingListener::Type::Digital, "button_analog", i);
            if (isListening) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                if (ImGui::Button("Waiting...")) m_Listener.CancelListening();
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("Waiting for input... Click to cancel.");
            } else {
                if (ImGui::Button("Bind"))
                    m_Listener.StartListening(InputBindingListener::Type::Digital, "button_analog", i, m_DeviceManager);
                ImGui::SetItemTooltip("Click to bind a button.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) bToDelete = i;
            if (rc) changed = true;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (bToDelete != -1) {
        profile.buttonMappings.erase(profile.buttonMappings.begin() + bToDelete);
        changed = true;
    }
}

void InputMapperUI::DrawChannelMixSection(MappingProfile& profile, const ProtocolDefinition* outDef,
                                           bool& changed) {
    if (!outDef) return;
    auto analogFields = GetEnabledFields(*outDef, FieldType::AnalogAxis);
    if (analogFields.empty()) return;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 1.f, 0.6f, 1.f), "Channel Mixes  (%s)", outDef->name.c_str());
    ImGui::TextWrapped("Combine multiple analog inputs (with individual weights) into a single analog output "
                        "field. Useful for mixing two pedal axes into one rudder output.");
    if (ImGui::Button("Add Channel Mix")) {
        profile.channelMixes.push_back({});
        changed = true;
    }

    int mixToDelete = -1;
    for (int mi = 0; mi < (int)profile.channelMixes.size(); ++mi) {
        auto& mix = profile.channelMixes[mi];
        bool rc = false;
        ImGui::PushID(8000 + mi);
        ImGui::Spacing();

        // ── Mix header: target field, clamp, delete ──────────────────
        std::string mixLabel = mix.target_field_id.empty() ? "None" : mix.target_field_id;
        for (auto& [pf2, fd2] : analogFields)
            if (pf2->fieldId == mix.target_field_id) { mixLabel = fd2->label; break; }

        ImGui::SetNextItemWidth(200.f);
        if (ImGui::BeginCombo("##mixField", mixLabel.c_str())) {
            if (ImGui::Selectable("None", mix.target_field_id.empty())) {
                mix.target_field_id = "";
                rc = true;
            }
            for (auto& [pf2, fd2] : analogFields) {
                bool s = mix.target_field_id == pf2->fieldId;
                if (ImGui::Selectable(fd2->label.c_str(), s)) {
                    mix.target_field_id = pf2->fieldId;
                    rc = true;
                }
                if (s) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Output analog field this mix writes to.");
        ImGui::SameLine();
        if (ImGui::Checkbox("Clamp [-1..1]", &mix.clamp_output)) rc = true;
        ImGui::SetItemTooltip("Clamp the summed output to the [-1, 1] range.");
        ImGui::SameLine();
        if (ImGui::Button("Add Source")) {
            mix.sources.push_back({});
            rc = true;
        }
        ImGui::SetItemTooltip("Add another analog source to this mix.");
        ImGui::SameLine();
        if (ImGui::Button("Delete Mix")) mixToDelete = mi;

        // ── Sources table ────────────────────────────────────────────
        if (!mix.sources.empty()) {
            if (ImGui::BeginTable("t_mix_src", 3,
                                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Axis Source", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 120.f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.f);
                ImGui::TableHeadersRow();

                int srcToDelete = -1;
                for (int si = 0; si < (int)mix.sources.size(); ++si) {
                    auto& ms = mix.sources[si];
                    ImGui::PushID(si);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    std::string mixSrcId = "__mix_" + std::to_string(mi) + "_" + std::to_string(si);
                    DrawAxisCombo(mixSrcId, ms.source, "##mixax", ImGui::GetContentRegionAvail().x, rc);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::SliderFloat("##mixw", &ms.weight, -2.f, 2.f, "%.2f")) rc = true;
                    ImGui::SetItemTooltip("Weight applied to this source before summing. -1 inverts the axis, 0 "
                                           "mutes it, 1 passes through.");

                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::Button("Del")) srcToDelete = si;

                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (srcToDelete != -1) {
                    mix.sources.erase(mix.sources.begin() + srcToDelete);
                    rc = true;
                }
            }

            // Live mix preview bar
            if (!mix.target_field_id.empty()) {
                float sum = 0.f;
                for (const auto& ms : mix.sources) sum += ReadInputSourceValue(ms.source, m_DeviceManager) * ms.weight;
                float clamped = mix.clamp_output ? std::clamp(sum, -1.f, 1.f) : sum;

                float barW = std::min(ImGui::GetContentRegionAvail().x, 400.f);
                ImVec2 bPos = ImGui::GetCursorScreenPos();
                float bH = ImGui::GetFrameHeight() * 0.6f;

                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(bPos, ImVec2(bPos.x + barW, bPos.y + bH), IM_COL32(50, 50, 50, 180), 3.f);

                float cx = bPos.x + barW * 0.5f;
                dl->AddLine(ImVec2(cx, bPos.y), ImVec2(cx, bPos.y + bH), IM_COL32(120, 120, 120, 180), 1.f);

                float frac = std::clamp((clamped + 1.f) * 0.5f, 0.f, 1.f);
                float fillX = bPos.x + barW * frac;
                bool over = !mix.clamp_output && (sum < -1.f || sum > 1.f);
                ImU32 fillCol = over ? IM_COL32(220, 80, 50, 200) : IM_COL32(60, 180, 100, 200);
                if (fillX > cx) dl->AddRectFilled(ImVec2(cx, bPos.y + 1), ImVec2(fillX, bPos.y + bH - 1), fillCol, 2.f);
                else dl->AddRectFilled(ImVec2(fillX, bPos.y + 1), ImVec2(cx, bPos.y + bH - 1), fillCol, 2.f);

                ImGui::Dummy(ImVec2(barW, bH));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60.f);
                ImGui::Text("%.3f", clamped);
                if (over) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.2f, 1), "(clipped)");
                }
            }
        }

        if (rc) changed = true;
        ImGui::PopID();
    }

    if (mixToDelete != -1) {
        profile.channelMixes.erase(profile.channelMixes.begin() + mixToDelete);
        changed = true;
    }
}

void InputMapperUI::DrawAnalogToDigitalSection(MappingProfile& profile, const ProtocolDefinition* outDef,
                                                bool& changed) {
    if (!outDef) return;
    auto digitalFields = GetEnabledFields(*outDef, FieldType::DigitalButton);
    if (digitalFields.empty()) return;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Analog to Digital Mappings");
    ImGui::TextWrapped("Drive a digital (0/1) output field from an analog axis or sensor when it crosses a threshold.");
    if (ImGui::Button("Add Analog->Digital Mapping")) {
        profile.analogToDigitalMappings.push_back({});
        changed = true;
    }

    int a2dToDelete = -1;
    if (ImGui::BeginTable("t_a2d", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Axis Source", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Digital Field", ImGuiTableColumnFlags_WidthFixed, 140.f);
        ImGui::TableSetupColumn("Threshold", ImGuiTableColumnFlags_WidthFixed, 160.f);
        ImGui::TableSetupColumn("Inv", ImGuiTableColumnFlags_WidthFixed, 30.f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 100.f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)profile.analogToDigitalMappings.size(); ++i) {
            auto& am = profile.analogToDigitalMappings[i];
            bool rc = false;
            ImGui::PushID(7000 + i);
            ImGui::TableNextRow();

            // Axis source column – DrawAxisCombo's internal Bind is suppressed here;
            // the standalone Bind/Delete column at the end handles binding for this row.
            ImGui::TableSetColumnIndex(0);
            std::string a2dId = "__a2d_" + std::to_string(i);
            DrawAxisCombo(a2dId, am.source, "##a2dax", ImGui::GetContentRegionAvail().x, rc, /*showBind=*/false);

            ImGui::TableSetColumnIndex(1);
            std::string flabel = am.target_field_id.empty() ? "None" : am.target_field_id;
            for (auto& [pf2, fd2] : digitalFields)
                if (pf2->fieldId == am.target_field_id) { flabel = fd2->label; break; }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##a2df", flabel.c_str())) {
                if (ImGui::Selectable("None", am.target_field_id.empty())) {
                    am.target_field_id = "";
                    rc = true;
                }
                for (auto& [pf2, fd2] : digitalFields) {
                    bool s = am.target_field_id == pf2->fieldId;
                    if (ImGui::Selectable(fd2->label.c_str(), s)) {
                        am.target_field_id = pf2->fieldId;
                        rc = true;
                    }
                    if (s) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Threshold column: SliderFloat with live input bar + threshold marker on foreground draw list
            ImGui::TableSetColumnIndex(2);
            {
                bool hasSrc = (am.source.instance_id != 0) &&
                              (am.source.axisIndex != -1 || am.source.sensorChannel != SC::None);
                float liveVal = hasSrc ? ReadInputSourceValue(am.source, m_DeviceManager) : 0.f;

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::SliderFloat("##a2dthr", &am.threshold, -1.f, 1.f, "Thr: %.2f")) rc = true;
                ImGui::SetItemTooltip("Threshold: axis must cross this value to activate.\nGreen bar = current "
                                       "live input level.");

                ImVec2 rMin = ImGui::GetItemRectMin();
                ImVec2 rMax = ImGui::GetItemRectMax();
                float w = rMax.x - rMin.x;
                ImDrawList* fg = ImGui::GetForegroundDrawList();

                float fillFrac = std::clamp((liveVal + 1.f) * 0.5f, 0.f, 1.f);
                ImU32 barCol = hasSrc ? IM_COL32(50, 200, 80, 110) : IM_COL32(80, 80, 80, 60);
                fg->AddRectFilled(rMin, ImVec2(rMin.x + w * fillFrac, rMax.y), barCol, 2.f);

                float thrFrac = std::clamp((am.threshold + 1.f) * 0.5f, 0.f, 1.f);
                float thrX = rMin.x + w * thrFrac;
                ImU32 thrCol = am.invert_threshold ? IM_COL32(255, 90, 60, 230) : IM_COL32(255, 220, 50, 230);
                fg->AddLine(ImVec2(thrX, rMin.y + 1.f), ImVec2(thrX, rMax.y - 1.f), thrCol, 2.f);
            }

            ImGui::TableSetColumnIndex(3);
            if (ImGui::Checkbox("##a2dinv", &am.invert_threshold)) rc = true;
            ImGui::SetItemTooltip("Active when value is BELOW threshold instead of above.");

            ImGui::TableSetColumnIndex(4);
            {
                const char* a2dModes[] = {"Momentary", "Toggle", "Set On", "Set Off"};
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##a2dmode", (int*)&am.mode, a2dModes, IM_ARRAYSIZE(a2dModes))) rc = true;
            }

            ImGui::TableSetColumnIndex(5);
            {
                bool isListeningA2D = m_Listener.IsListeningFor(InputBindingListener::Type::Axis, a2dId);
                if (isListeningA2D) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
                    if (ImGui::Button("Waiting...")) m_Listener.CancelListening();
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("Waiting for input... Click to cancel.");
                } else {
                    if (ImGui::Button("Bind"))
                        m_Listener.StartListening(InputBindingListener::Type::Axis, a2dId, -1, m_DeviceManager);
                    ImGui::SetItemTooltip("Click to bind an analog axis.");
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete")) a2dToDelete = i;
            }

            if (rc) changed = true;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (a2dToDelete != -1) {
        profile.analogToDigitalMappings.erase(profile.analogToDigitalMappings.begin() + a2dToDelete);
        changed = true;
    }
}

} // namespace InputMapping