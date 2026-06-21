#include "MappingProfileStore.h"

#include "App/Log.h"
#include "Devices/DeviceManager.h"
#include "Mappers/OutputMapper.h"
#include "Network/OSCServer.h"
#include "Network/WebSocketServer.h"
#include "Preferences/Preferences.h"
#include "Protocols/OSCProjectBabbleProtocol.h"
#include "Protocols/OSCSteamLinkProtocol.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"
#include "Utils/XdgDirs.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

static constexpr const char* kTag = "InputMapper";

using json = nlohmann::json;

namespace InputMapping {

// NOTE: these macros MUST live inside namespace InputMapping. nlohmann's
// json::value<T>()/get<T>() find the macro-generated to_json/from_json
// overloads via ADL, which only searches namespaces associated with T.
// Declaring them at global scope (as before) made them invisible to ADL
// for InputMapping::ButtonToDigitalMapping::Mode /
// InputMapping::AnalogToDigitalMapping::Mode, silently falling back to
// nlohmann's default "treat enum as its underlying int" handling — which
// throws type_error.302 the moment it hits a "mode": "momentary" string.
NLOHMANN_JSON_SERIALIZE_ENUM(ButtonToDigitalMapping::Mode, {
    {ButtonToDigitalMapping::Mode::Momentary, "momentary"},
    {ButtonToDigitalMapping::Mode::Toggle,    "toggle"},
    {ButtonToDigitalMapping::Mode::SetOn,     "set_on"},
    {ButtonToDigitalMapping::Mode::SetOff,    "set_off"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(AnalogToDigitalMapping::Mode, {
    {AnalogToDigitalMapping::Mode::Momentary, "momentary"},
    {AnalogToDigitalMapping::Mode::Toggle,    "toggle"},
    {AnalogToDigitalMapping::Mode::SetOn,     "set_on"},
    {AnalogToDigitalMapping::Mode::SetOff,    "set_off"},
})

namespace {

std::filesystem::path GetMappingsDirectory() {
    // Mapping profiles are user data — they belong in
    // $XDG_DATA_HOME/InputBridge/mappings/ per the XDG Base Directory
    // Specification (fallback: ~/.local/share/InputBridge/mappings/).
    return std::filesystem::path(XdgDirs::dataDir()) / "mappings";
}

// ── JSON ⇄ InputSource ──────────────────────────────────────────────────────
// All three places an InputSource is embedded in the profile JSON (the
// top-level "mappings" map, an analog→digital mapping's "source", and a
// channel-mix source) use the exact same key names, so one pair of
// converters covers everything.

InputSource ParseInputSource(const json& val) {
    InputSource src;
    src.deviceGuid = val.value("device_guid", std::string());
    src.axisIndex = val.value("axis", -1);
    src.invert = val.value("invert", false);
    src.deadzone = val.value("deadzone", 0.05f);
    src.outputRange = val.value("range", 0);
    src.sensorChannel = static_cast<InputSource::SensorChannel>(
        val.value("sensor_channel", static_cast<int>(InputSource::SensorChannel::None)));
    return src;
}

json SerializeInputSource(const InputSource& src) {
    return {
        {"device_guid",    src.deviceGuid},
        {"axis",           src.axisIndex},
        {"invert",         src.invert},
        {"deadzone",       src.deadzone},
        {"range",          src.outputRange},
        {"sensor_channel", static_cast<int>(src.sensorChannel)},
    };
}

// ── JSON ⇄ MappingProfile, one collection at a time ────────────────────────

void ParseOutputMappings(const json& data, MappingProfile& p) {
    if (!data.contains("mappings")) return;
    for (auto& [key, val] : data["mappings"].items())
        p.outputToInput[key] = ParseInputSource(val);
}

void ParseHapticTargets(const json& data, MappingProfile& p) {
    if (!data.contains("haptic_targets")) return;
    for (const auto& item : data["haptic_targets"]) {
        HapticTarget t;
        t.virtual_id = item.value("virtual_id", 0);
        t.name = item.value("name", "Target");
        t.device_guid = item.value("device_guid", "");
        t.enable_rumble = item.value("enable_rumble", true);
        t.enable_constant = item.value("enable_constant", true);
        t.enable_periodic = item.value("enable_periodic", true);
        t.enable_condition = item.value("enable_condition", true);
        p.hapticTargets.push_back(t);
    }
}

void ParseButtonMappings(const json& data, MappingProfile& p) {
    if (!data.contains("button_mappings")) return;
    for (const auto& item : data["button_mappings"]) {
        ButtonToAnalogMapping bm;
        bm.device_guid = item.value("device_guid", "");
        bm.button_index = item.value("button_index", -1);
        bm.hat_index = item.value("hat_index", -1);
        bm.hat_mask = item.value("hat_mask", 0);
        bm.target_output_name = item.value("target_output_name", "");
        bm.sensor_channel = static_cast<InputSource::SensorChannel>(
            item.value("sensor_channel", static_cast<int>(InputSource::SensorChannel::None)));
        bm.on_value = item.value("on_value", 1.f);
        bm.off_value = item.value("off_value", 0.f);
        p.buttonMappings.push_back(bm);
    }
}

void ParseDigitalMappings(const json& data, MappingProfile& p) {
    if (!data.contains("digital_mappings")) return;
    for (const auto& item : data["digital_mappings"]) {
        ButtonToDigitalMapping dm;
        dm.device_guid = item.value("device_guid", "");
        dm.button_index = item.value("button_index", -1);
        dm.hat_index = item.value("hat_index", -1);
        dm.hat_mask = item.value("hat_mask", 0);
        dm.sensor_channel = static_cast<InputSource::SensorChannel>(
            item.value("sensor_channel", static_cast<int>(InputSource::SensorChannel::None)));
        dm.target_field_id = item.value("target_field_id", "");
        if (item.contains("mode")) {
            dm.mode = item.value("mode", ButtonToDigitalMapping::Mode::Momentary);
        } else if (item.value("is_toggle", false)) {
            dm.mode = ButtonToDigitalMapping::Mode::Toggle;
        }
        p.digitalMappings.push_back(dm);
    }
}

void ParseAnalogToDigitalMappings(const json& data, MappingProfile& p) {
    if (!data.contains("analog_to_digital_mappings")) return;
    for (const auto& item : data["analog_to_digital_mappings"]) {
        AnalogToDigitalMapping am;
        am.source = ParseInputSource(item);
        am.target_field_id = item.value("target_field_id", "");
        am.threshold = item.value("threshold", 0.5f);
        am.invert_threshold = item.value("invert_threshold", false);
        am.mode = item.value("mode", AnalogToDigitalMapping::Mode::Momentary);
        p.analogToDigitalMappings.push_back(am);
    }
}

void ParseChannelMixes(const json& data, MappingProfile& p) {
    if (!data.contains("channel_mixes")) return;
    for (const auto& item : data["channel_mixes"]) {
        ChannelMix mix;
        mix.target_field_id = item.value("target_field_id", "");
        mix.clamp_output = item.value("clamp_output", true);
        if (item.contains("sources")) {
            for (const auto& s : item["sources"]) {
                ChannelMix::MixSource ms;
                ms.source = ParseInputSource(s);
                ms.weight = s.value("weight", 1.0f);
                mix.sources.push_back(ms);
            }
        }
        p.channelMixes.push_back(mix);
    }
}

void ParseServerSettings(const json& data, MappingProfile& p) {
    if (data.contains("digital_toggle_states")) {
        for (const auto& [key, val] : data["digital_toggle_states"].items())
            if (val.is_boolean()) p.digitalToggleStates[key] = val.get<bool>();
    }

    p.oscOutputProtocolId = data.value("osc_output_protocol_id", "");
    p.oscInputProtocolId = data.value("osc_input_protocol_id", "");
    p.wsOutputProtocolId = data.value("ws_output_protocol_id", "");
    p.wsInputProtocolId = data.value("ws_input_protocol_id", "");
    p.selectedProtocolView = data.value("selected_protocol_view", 0);

    p.oscSendHost = data.value("osc_send_host", "127.0.0.1");
    p.oscSendPort = data.value("osc_send_port", 9066);
    p.oscRecvPort = data.value("osc_recv_port", 9068);
    p.oscOutputEnabled = data.value("osc_output_enabled", true);
    p.oscInputEnabled = data.value("osc_input_enabled", true);

    p.wsPort = data.value("ws_port", 4269);
    p.wsOutputEnabled = data.value("ws_output_enabled", true);
    p.wsInputEnabled = data.value("ws_input_enabled", true);
}

// Parses one profile JSON document. Throws on malformed/missing "name",
// matching the original LoadProfiles' "skip this file" behaviour via the
// caller's try/catch.
MappingProfile ParseProfile(const json& data) {
    if (!data.contains("name") || !data["name"].is_string() || data["name"].get<std::string>().empty())
        throw std::runtime_error("profile JSON missing a non-empty \"name\"");

    MappingProfile p;
    p.name = data["name"];
    ParseOutputMappings(data, p);
    ParseHapticTargets(data, p);
    ParseButtonMappings(data, p);
    ParseDigitalMappings(data, p);
    ParseAnalogToDigitalMappings(data, p);
    ParseChannelMixes(data, p);
    ParseServerSettings(data, p);
    return p;
}

void SerializeOutputMappings(const MappingProfile& profile, json& data) {
    data["mappings"] = json::object();
    for (const auto& [k, v] : profile.outputToInput) {
        bool hasSensor = (v.sensorChannel != InputSource::SensorChannel::None);
        if (v.axisIndex != -1 || hasSensor) data["mappings"][k] = SerializeInputSource(v);
    }
}

void SerializeHapticTargets(const MappingProfile& profile, json& data) {
    data["haptic_targets"] = json::array();
    for (const auto& t : profile.hapticTargets) {
        data["haptic_targets"].push_back({
            {"virtual_id", t.virtual_id}, {"name", t.name}, {"device_guid", t.device_guid},
            {"enable_rumble", t.enable_rumble}, {"enable_constant", t.enable_constant},
            {"enable_periodic", t.enable_periodic}, {"enable_condition", t.enable_condition},
        });
    }
}

void SerializeButtonMappings(const MappingProfile& profile, json& data) {
    data["button_mappings"] = json::array();
    for (const auto& bm : profile.buttonMappings) {
        json j = {
            {"device_guid", bm.device_guid}, {"target_output_name", bm.target_output_name},
            {"on_value", bm.on_value}, {"off_value", bm.off_value},
        };
        if (bm.hat_index != -1) { j["hat_index"] = bm.hat_index; j["hat_mask"] = bm.hat_mask; }
        else if (bm.button_index != -1) { j["button_index"] = bm.button_index; }
        if (bm.sensor_channel != InputSource::SensorChannel::None)
            j["sensor_channel"] = static_cast<int>(bm.sensor_channel);
        data["button_mappings"].push_back(j);
    }
}

void SerializeDigitalMappings(const MappingProfile& profile, json& data) {
    data["digital_mappings"] = json::array();
    for (const auto& dm : profile.digitalMappings) {
        json j = {
            {"device_guid", dm.device_guid}, {"target_field_id", dm.target_field_id}, {"mode", dm.mode},
        };
        if (dm.hat_index != -1) { j["hat_index"] = dm.hat_index; j["hat_mask"] = dm.hat_mask; }
        else if (dm.button_index != -1) { j["button_index"] = dm.button_index; }
        if (dm.sensor_channel != InputSource::SensorChannel::None)
            j["sensor_channel"] = static_cast<int>(dm.sensor_channel);
        data["digital_mappings"].push_back(j);
    }
}

void SerializeAnalogToDigitalMappings(const MappingProfile& profile, json& data) {
    data["analog_to_digital_mappings"] = json::array();
    for (const auto& am : profile.analogToDigitalMappings) {
        json j = SerializeInputSource(am.source);
        j["target_field_id"] = am.target_field_id;
        j["threshold"] = am.threshold;
        j["invert_threshold"] = am.invert_threshold;
        j["mode"] = am.mode;
        data["analog_to_digital_mappings"].push_back(j);
    }
}

void SerializeChannelMixes(const MappingProfile& profile, json& data) {
    data["channel_mixes"] = json::array();
    for (const auto& mix : profile.channelMixes) {
        if (mix.target_field_id.empty() || mix.sources.empty()) continue;
        json srcs = json::array();
        for (const auto& ms : mix.sources) {
            json j = SerializeInputSource(ms.source);
            j["weight"] = ms.weight;
            srcs.push_back(j);
        }
        data["channel_mixes"].push_back({
            {"target_field_id", mix.target_field_id},
            {"clamp_output", mix.clamp_output},
            {"sources", srcs},
        });
    }
}

void SerializeServerSettings(const MappingProfile& profile, json& data) {
    data["digital_toggle_states"] = profile.digitalToggleStates;

    data["osc_output_protocol_id"] = profile.oscOutputProtocolId;
    data["osc_input_protocol_id"] = profile.oscInputProtocolId;
    data["ws_output_protocol_id"] = profile.wsOutputProtocolId;
    data["ws_input_protocol_id"] = profile.wsInputProtocolId;
    data["selected_protocol_view"] = profile.selectedProtocolView;

    data["osc_send_host"] = profile.oscSendHost;
    data["osc_send_port"] = profile.oscSendPort;
    data["osc_recv_port"] = profile.oscRecvPort;
    data["osc_output_enabled"] = profile.oscOutputEnabled;
    data["osc_input_enabled"] = profile.oscInputEnabled;

    data["ws_port"] = profile.wsPort;
    data["ws_output_enabled"] = profile.wsOutputEnabled;
    data["ws_input_enabled"] = profile.wsInputEnabled;
}

json SerializeProfile(const MappingProfile& profile) {
    json data;
    data["name"] = profile.name;
    SerializeOutputMappings(profile, data);
    SerializeHapticTargets(profile, data);
    SerializeButtonMappings(profile, data);
    SerializeDigitalMappings(profile, data);
    SerializeAnalogToDigitalMappings(profile, data);
    SerializeChannelMixes(profile, data);
    SerializeServerSettings(profile, data);
    return data;
}

// Seeds bundled preset profiles from the read-only install directory into
// the writable data dir on first run, skipping files that already exist.
// SDL_GetBasePath() points to the read-only squashfs mount inside an
// AppImage, which is exactly where the installed mappings/ dir lives.
void SeedBundledPresets(const std::filesystem::path& dir) {
    const char* base = SDL_GetBasePath();
    if (!base) return;

    auto installDir = std::filesystem::path(base) / "mappings";
    if (!std::filesystem::exists(installDir)) return;

    for (const auto& e : std::filesystem::directory_iterator(installDir)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        auto dst = dir / e.path().filename();
        if (std::filesystem::exists(dst)) continue;
        std::error_code ec;
        std::filesystem::copy_file(e.path(), dst, std::filesystem::copy_options::skip_existing, ec);
        if (ec)
            LOG_ERROR(kTag, "Failed to seed preset profile %s: %s",
                      e.path().filename().string().c_str(), ec.message().c_str());
    }
}

// Resolves the live ProtocolDefinition currently driving OSC output,
// following the same id → registry lookup → SteamLink/ProjectBabble
// built-in-fallback chain used by GetActiveOutputDefinition. Recomputed on
// every call (not cached past the first) because CreateDefaultDefinition()
// pulls in whatever custom fields are currently in the field catalog.
const ProtocolDefinition* ResolveOscFallbackDefinition(const std::string& protocol) {
    if (protocol == "SteamLink OSC") {
        static ProtocolDefinition s_steamLinkDef;
        s_steamLinkDef = OSCSteamLinkProtocol::CreateDefaultDefinition();
        return &s_steamLinkDef;
    }
    if (protocol == "Project Babble OSC") {
        static ProtocolDefinition s_projectBabbleDef;
        s_projectBabbleDef = OSCProjectBabbleProtocol::CreateDefaultDefinition();
        return &s_projectBabbleDef;
    }
    return nullptr;
}

} // namespace

MappingProfileStore::MappingProfileStore(const DeviceManager& deviceManager)
    : m_DeviceManager(deviceManager) {}

void MappingProfileStore::LoadProfiles() {
    m_Profiles.clear();
    try {
        auto dir = GetMappingsDirectory();
        if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);

        SeedBundledPresets(dir);

        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            if (!e.is_regular_file() || e.path().extension() != ".json") continue;
            std::ifstream f(e.path());
            if (!f) continue;
            try {
                m_Profiles.push_back(ParseProfile(json::parse(f)));
            } catch (const std::exception& ex) {
                LOG_ERROR(kTag, "Failed to parse profile %s: %s", e.path().string().c_str(), ex.what());
            }
        }
    } catch (const std::exception& ex) {
        LOG_ERROR(kTag, "Failed to load profiles: %s", ex.what());
    }
}

void MappingProfileStore::SaveProfile(const MappingProfile& profile) const {
    try {
        auto dir = GetMappingsDirectory();
        if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
        auto path = dir / (profile.name + ".json");
        std::ofstream o(path);
        if (o) o << SerializeProfile(profile).dump(4);
    } catch (const std::exception& ex) {
        LOG_ERROR(kTag, "Failed to save profile: %s", ex.what());
    }
}

void MappingProfileStore::SaveCurrentProfile() {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) return;
    auto& profile = m_Profiles[m_SelectedProfileIndex];
    SnapshotServerSettings(profile);
    SaveProfile(profile);
}

void MappingProfileStore::LoadConfig(PreferencesManager& prefs) {
    // Null the OutputMapper pointer before LoadProfiles() calls m_Profiles.clear()
    // followed by many push_backs. Any of those reallocations would leave the
    // OutputMapper holding a dangling pointer into the old buffer.
    OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
    LoadProfiles();
    std::string last = prefs.GetString("InputMapper", "LastProfile", "");
    int restoredIdx = -1;
    for (int i = 0; i < (int)m_Profiles.size(); ++i)
        if (m_Profiles[i].name == last) { restoredIdx = i; break; }
    if (restoredIdx == -1) {
        m_SelectedProtocolView = prefs.GetInt("InputMapper", "SelectedProtocolView", 0);
    }
    ActivateProfile(restoredIdx);
    HandleDeviceConnectionChange();
}

void MappingProfileStore::SaveConfig(PreferencesManager& prefs) const {
    prefs.SetString("InputMapper", "LastProfile",
        m_SelectedProfileIndex >= 0 && m_SelectedProfileIndex < (int)m_Profiles.size()
            ? m_Profiles[m_SelectedProfileIndex].name : "");
    prefs.SetInt("InputMapper", "SelectedProtocolView", m_SelectedProtocolView);
}

int MappingProfileStore::CreateProfile(const std::string& name) {
    MappingProfile p;
    p.name = name;
    SnapshotServerSettings(p);
    // Null the pointer BEFORE push_back: if the vector reallocates its
    // buffer, SetActiveHapticTargets would otherwise iterate through the old
    // (now freed) memory when closing the previous targets.
    OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
    m_Profiles.push_back(p);
    int newIdx = (int)m_Profiles.size() - 1;
    SaveProfile(p);
    ActivateProfile(newIdx);
    return newIdx;
}

bool MappingProfileStore::RenameProfile(int index, const std::string& newName) {
    if (index < 0 || index >= (int)m_Profiles.size() || newName.empty()) return false;
    const std::string oldName = m_Profiles[index].name;
    if (newName == oldName) return true;

    auto dir = GetMappingsDirectory();
    auto oldPath = dir / (oldName + ".json");
    auto newPath = dir / (newName + ".json");

    m_Profiles[index].name = newName;
    SaveProfile(m_Profiles[index]);

    std::error_code ec;
    if (std::filesystem::exists(oldPath, ec)) {
        if (!std::filesystem::exists(newPath, ec) || !std::filesystem::equivalent(oldPath, newPath, ec))
            std::filesystem::remove(oldPath, ec);
    }
    return true;
}

void MappingProfileStore::DeleteProfile(int index) {
    if (index < 0 || index >= (int)m_Profiles.size()) return;
    try {
        auto p = GetMappingsDirectory() / (m_Profiles[index].name + ".json");
        if (std::filesystem::exists(p)) std::filesystem::remove(p);
    } catch (...) {}
    // Null the pointer BEFORE erase: erase invalidates all iterators and
    // pointers into the vector, so SetActiveHapticTargets must not
    // dereference the old pointer when closing haptic devices on the
    // deleted profile.
    OutputMapper::GetInstance().SetActiveHapticTargets(nullptr);
    m_Profiles.erase(m_Profiles.begin() + index);
    ActivateProfile(-1);
}

void MappingProfileStore::ActivateProfile(int index) {
    // Guard against out-of-range indices.
    if (index != -1 && (index < 0 || index >= (int)m_Profiles.size())) return;

    m_SelectedProfileIndex = index;

    // --- Atomic profile swap ------------------------------------------------
    // All subsystems are updated together so no frame can observe a mixed state
    // (e.g. haptic targets from profile A with axis mappings from profile B).
    //
    // 1. Hand the new haptic targets to OutputMapper immediately — it may be
    //    playing effects from the old profile and must stop them before the
    //    pointer changes.
    OutputMapper::GetInstance().SetActiveHapticTargets(
        index != -1 ? &m_Profiles[index].hapticTargets : nullptr);

    // 2. Restore per-profile UI state.
    if (index != -1) m_SelectedProtocolView = m_Profiles[index].selectedProtocolView;

    // Remap hardware IDs immediately on activation so the first Update() tick is valid.
    HandleDeviceConnectionChange();

    // 3. Push the new protocol IDs and server settings to both servers.
    UpdateActiveProtocols();
}

void MappingProfileStore::HandleDeviceConnectionChange() {
    std::map<std::string, SDL_JoystickID> guidMap;
    for (const auto& d : m_DeviceManager.GetDevices())
        guidMap[DeviceManager::GetDeviceGUIDString(d)] = d.instance_id;

    auto remap = [&](const std::string& guid) -> SDL_JoystickID {
        auto it = guidMap.find(guid);
        return it != guidMap.end() ? it->second : 0;
    };

    for (auto& p : m_Profiles) {
        for (auto& [k, src] : p.outputToInput) src.instance_id = remap(src.deviceGuid);
        for (auto& t : p.hapticTargets)
            if (!t.device_guid.empty()) t.instance_id = remap(t.device_guid);
        for (auto& bm : p.buttonMappings)
            if (!bm.device_guid.empty()) bm.instance_id = remap(bm.device_guid);
        for (auto& dm : p.digitalMappings)
            if (!dm.device_guid.empty()) dm.instance_id = remap(dm.device_guid);
        for (auto& am : p.analogToDigitalMappings)
            if (!am.source.deviceGuid.empty()) am.source.instance_id = remap(am.source.deviceGuid);
        for (auto& mix : p.channelMixes)
            for (auto& ms : mix.sources)
                if (!ms.source.deviceGuid.empty()) ms.source.instance_id = remap(ms.source.deviceGuid);
    }
}

void MappingProfileStore::UpdateActiveProtocols() {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) {
        ProtocolManager::GetInstance().SetActiveInputProtocolId("");
        return;
    }

    const auto& p = m_Profiles[m_SelectedProfileIndex];
    ProtocolManager::GetInstance().SetActiveInputProtocolId(p.oscInputProtocolId);

    // ── OSC server ────────────────────────────────────────────────────────────
    auto& osc = OSCServer::GetInstance();
    osc.SetOutputDefinition(p.oscOutputProtocolId);
    osc.SetInputDefinition(p.oscInputProtocolId);

    // Apply enable flags and port settings.  Ports only take effect after a
    // restart, so we write them to the UI fields without restarting.
    osc.SetOutputEnabled(p.oscOutputEnabled);
    osc.SetInputEnabled(p.oscInputEnabled);
    osc.SetPortsFromProfile(p.oscSendHost, p.oscSendPort, p.oscRecvPort);

#ifdef ENABLE_WEBSOCKETS
    // ── WebSocket server ──────────────────────────────────────────────────────
    auto& ws = WebSocketServer::GetInstance();
    ws.SetOutputDefinition(p.wsOutputProtocolId);
    ws.SetInputDefinition(p.wsInputProtocolId);
    ws.SetOutputEnabled(p.wsOutputEnabled);
    ws.SetInputEnabled(p.wsInputEnabled);
    ws.SetPortFromProfile(p.wsPort);
#endif
}

void MappingProfileStore::SnapshotServerSettings(MappingProfile& profile) const {
    auto& osc = OSCServer::GetInstance();
    profile.oscOutputProtocolId = osc.GetOutputDefinitionId();
    profile.oscInputProtocolId  = osc.GetInputDefinitionId();
    profile.oscSendHost         = osc.GetSendHost();
    profile.oscSendPort         = osc.GetSendPort();
    profile.oscRecvPort         = osc.GetReceivePort();
    profile.oscOutputEnabled    = osc.IsOutputEnabled();
    profile.oscInputEnabled     = osc.IsInputEnabled();

#ifdef ENABLE_WEBSOCKETS
    auto& ws = WebSocketServer::GetInstance();
    profile.wsOutputProtocolId = ws.GetOutputDefinitionId();
    profile.wsInputProtocolId  = ws.GetInputDefinitionId();
    profile.wsPort             = ws.GetPort();
    profile.wsOutputEnabled    = ws.IsOutputEnabled();
    profile.wsInputEnabled     = ws.IsInputEnabled();
#endif
}

MappingProfile* MappingProfileStore::CurrentProfile() {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) return nullptr;
    return &m_Profiles[m_SelectedProfileIndex];
}

const MappingProfile* MappingProfileStore::CurrentProfile() const {
    if (m_SelectedProfileIndex < 0 || m_SelectedProfileIndex >= (int)m_Profiles.size()) return nullptr;
    return &m_Profiles[m_SelectedProfileIndex];
}

std::vector<::HapticTarget>* MappingProfileStore::GetCurrentHapticTargets() {
    auto* p = CurrentProfile();
    return p ? &p->hapticTargets : nullptr;
}

const ProtocolDefinition* MappingProfileStore::GetActiveOutputDefinition() const {
    std::string oscId, wsId;
    if (const auto* profile = CurrentProfile()) {
        oscId = profile->oscOutputProtocolId;
        wsId = profile->wsOutputProtocolId;
    }

    if (m_SelectedProtocolView == 0) { // OSC
        if (!oscId.empty()) return ProtocolRegistry::GetInstance().FindById(oscId);
        return ResolveOscFallbackDefinition(OSCServer::GetInstance().GetProtocol());
    }
    // WebSocket
    if (!wsId.empty()) return ProtocolRegistry::GetInstance().FindById(wsId);
    return nullptr;
}

bool MappingProfileStore::IsOutputAddressBound(const std::string& address) const {
    const auto* profilePtr = CurrentProfile();
    if (!profilePtr) return false; // No profile, so nothing is bound.
    const auto& profile = *profilePtr;

    std::string fieldId;

    // 1. Check active protocol definitions from the profile
    const ProtocolDefinition* oscDef = !profile.oscOutputProtocolId.empty()
        ? ProtocolRegistry::GetInstance().FindById(profile.oscOutputProtocolId) : nullptr;
    if (oscDef) {
        for (const auto& field : oscDef->fields)
            if (field.oscPath == address) { fieldId = field.fieldId; break; }
    }

    if (fieldId.empty()) {
        const ProtocolDefinition* wsDef = !profile.wsOutputProtocolId.empty()
            ? ProtocolRegistry::GetInstance().FindById(profile.wsOutputProtocolId) : nullptr;
        if (wsDef) {
            for (const auto& field : wsDef->fields)
                if (field.wsKey == address) { fieldId = field.fieldId; break; }
        }
    }

    // 2. If not found, check legacy protocols
    if (fieldId.empty()) {
        // This mapping is for legacy protocols that don't use definitions.
        if (address == "/wheel/steer" || address == "wheel") fieldId = "Steering";
        else if (address == "/wheel/throttle" || address == "throttle") fieldId = "Throttle";
        else if (address == "/wheel/brake" || address == "brake") fieldId = "Brake";
        else if (address == "/wheel/clutch" || address == "clutch") fieldId = "Clutch";
        else if (address == "/wheel/handbrake" || address == "handbrake") fieldId = "Handbrake";
        else if (address == "/wheel/pitch" || address == "pitch") fieldId = "Pitch";
        else if (address == "/wheel/roll" || address == "roll") fieldId = "Roll";
    }

    if (fieldId.empty()) return false;

    // 3. Check if fieldId is bound in the current profile
    if (profile.outputToInput.count(fieldId)) {
        const auto& src = profile.outputToInput.at(fieldId);
        if (src.axisIndex != -1 || src.sensorChannel != InputSource::SensorChannel::None) return true;
    }
    for (const auto& mapping : profile.buttonMappings) {
        if (mapping.target_output_name == fieldId &&
            (mapping.button_index != -1 || mapping.hat_index != -1 ||
             mapping.sensor_channel != InputSource::SensorChannel::None))
            return true;
    }
    for (const auto& mapping : profile.digitalMappings) {
        if (mapping.target_field_id == fieldId &&
            (mapping.button_index != -1 || mapping.hat_index != -1 ||
             mapping.sensor_channel != InputSource::SensorChannel::None))
            return true;
    }
    for (const auto& mapping : profile.analogToDigitalMappings) {
        if (mapping.target_field_id == fieldId &&
            (mapping.source.axisIndex != -1 || mapping.source.sensorChannel != InputSource::SensorChannel::None))
            return true;
    }
    for (const auto& mix : profile.channelMixes) {
        if (mix.target_field_id == fieldId && !mix.sources.empty()) {
            for (const auto& ms : mix.sources)
                if (ms.source.axisIndex != -1 || ms.source.sensorChannel != InputSource::SensorChannel::None)
                    return true;
        }
    }

    return false;
}

} // namespace InputMapping