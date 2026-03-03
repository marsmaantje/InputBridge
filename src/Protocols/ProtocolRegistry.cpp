#include "ProtocolRegistry.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <random>
#include <SDL3/SDL_filesystem.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ─── Singleton ───────────────────────────────────────────────────────────────

ProtocolRegistry& ProtocolRegistry::GetInstance() {
    static ProtocolRegistry instance;
    return instance;
}

ProtocolRegistry::ProtocolRegistry() {
    EnsureDirectories();
    LoadBuiltinCatalog();
}

// ─── Public API ──────────────────────────────────────────────────────────────

void ProtocolRegistry::LoadAll() {
    EnsureDirectories();
    LoadFieldCatalog();
    LoadPresets();
    LoadDefinitionFiles();
}

void ProtocolRegistry::SaveAll() {
    for (const auto& def : m_definitions) {
        SaveDefinition(def);
    }
}

const std::vector<FieldDescriptor>& ProtocolRegistry::GetOutputFields() const {
    return m_outputFields;
}

const std::vector<FieldDescriptor>& ProtocolRegistry::GetInputFields() const {
    return m_inputFields;
}

void ProtocolRegistry::ReloadFieldCatalog() {
    LoadBuiltinCatalog();
    LoadFieldCatalog();
}

void ProtocolRegistry::AddOutputField(const FieldDescriptor& fd) {
    for (auto& f : m_outputFields) {
        if (f.id == fd.id) return; // Already exists
    }
    m_outputFields.push_back(fd);
    SaveFieldCatalog();
}

void ProtocolRegistry::DeleteOutputField(const std::string& id) {
    auto it = std::remove_if(m_outputFields.begin(), m_outputFields.end(),
                             [&](const FieldDescriptor& fd) { return fd.id == id && !fd.isBuiltIn; });
    if (it != m_outputFields.end()) {
        m_outputFields.erase(it, m_outputFields.end());
        SaveFieldCatalog();
    }
}

void ProtocolRegistry::SaveFieldCatalog() {
    json j;
    json arr = json::array();
    for (const auto& fd : m_outputFields) {
        if (fd.isBuiltIn) continue;
        json item;
        item["id"] = fd.id;
        item["label"] = fd.label;
        item["category"] = fd.category;
        item["type"] = (fd.type == FieldType::DigitalButton) ? "digital" : "analog";
        item["oscPath"] = fd.defaultOscPath;
        item["wsKey"] = fd.defaultWsKey;
        arr.push_back(item);
    }
    j["output_fields"] = arr;

    std::string path = GetProtocolsDir() + "input_fields.json";
    std::ofstream ofs(path);
    if (ofs) ofs << j.dump(4);
}

const std::vector<FieldPreset>& ProtocolRegistry::GetPresets() const {
    return m_presets;
}

void ProtocolRegistry::SavePreset(const std::string& name, const std::vector<std::string>& fieldIds) {
    for (auto& p : m_presets) {
        if (p.name == name) {
            p.fieldIds = fieldIds;
            SavePresets();
            return;
        }
    }
    FieldPreset p;
    p.name = name;
    p.fieldIds = fieldIds;
    m_presets.push_back(p);
    SavePresets();
}

void ProtocolRegistry::DeletePreset(const std::string& name) {
    m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(),
                                   [&](const FieldPreset& p) { return p.name == name; }),
                    m_presets.end());
    SavePresets();
}

std::vector<ProtocolDefinition>& ProtocolRegistry::GetDefinitions() {
    return m_definitions;
}

const std::vector<ProtocolDefinition>& ProtocolRegistry::GetDefinitions() const {
    return m_definitions;
}

ProtocolDefinition* ProtocolRegistry::FindById(const std::string& id) {
    for (auto& d : m_definitions)
        if (d.id == id) return &d;
    return nullptr;
}

const ProtocolDefinition* ProtocolRegistry::FindById(const std::string& id) const {
    for (const auto& d : m_definitions)
        if (d.id == id) return &d;
    return nullptr;
}

std::string ProtocolRegistry::CreateDefinition(const std::string& name,
                                                ProtocolTransport  transport,
                                                ProtocolDirection  direction) {
    ProtocolDefinition def;
    def.id        = GenerateId();
    def.name      = name;
    def.transport = transport;
    def.direction = direction;
    m_definitions.push_back(def);
    SaveDefinition(m_definitions.back());
    return def.id;
}

std::string ProtocolRegistry::DuplicateDefinition(const std::string& srcId, const std::string& newName, ProtocolTransport newTransport) {
    ProtocolDefinition* src = FindById(srcId);
    if (!src) return "";

    ProtocolDefinition def = *src;
    def.id = GenerateId();
    def.name = newName;
    def.transport = newTransport;
    def.active = false; // Don't activate duplicate immediately

    m_definitions.push_back(def);
    SaveDefinition(m_definitions.back());
    return def.id;
}

bool ProtocolRegistry::ExportDefinition(const std::string& id, const std::string& path) {
    const ProtocolDefinition* def = FindById(id);
    if (!def) return false;

    json j;
    j["id"]        = def->id;
    j["name"]      = def->name;
    j["transport"] = (def->transport == ProtocolTransport::OSC) ? "osc" : "websocket";
    j["direction"] = (def->direction == ProtocolDirection::Output) ? "output" : "input";
    j["active"]    = false; // Don't activate exported protocols by default

    j["osc"]["host"]     = def->oscHost;
    j["osc"]["sendPort"] = def->oscSendPort;
    j["osc"]["recvPort"] = def->oscRecvPort;
    j["ws"]["port"]      = def->wssPort;

    json fieldsArr = json::array();
    for (const auto& f : def->fields) {
        json fj;
        fj["fieldId"] = f.fieldId;
        fj["oscPath"] = f.oscPath;
        fj["wsKey"]   = f.wsKey;
        fj["enabled"] = f.enabled;
        fieldsArr.push_back(fj);
    }
    j["fields"] = fieldsArr;

    std::ofstream ofs(path);
    if (!ofs) return false;
    ofs << j.dump(4);
    return true;
}

std::string ProtocolRegistry::ImportDefinition(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return "";

    try {
        json j = json::parse(ifs);
        ProtocolDefinition def;
        def.id = GenerateId(); // Always generate a new ID to avoid conflicts
        def.name = j.value("name", "Imported Protocol");

        std::string ts = j.value("transport", "osc");
        def.transport = (ts == "websocket") ? ProtocolTransport::WebSocket : ProtocolTransport::OSC;

        std::string dir_s = j.value("direction", "output");
        def.direction = (dir_s == "input") ? ProtocolDirection::Input : ProtocolDirection::Output;

        def.active = false;

        if (j.contains("osc")) {
            def.oscHost     = j["osc"].value("host",     "127.0.0.1");
            def.oscSendPort = j["osc"].value("sendPort", 9066);
            def.oscRecvPort = j["osc"].value("recvPort", 9068);
        }
        if (j.contains("ws")) {
            def.wssPort = j["ws"].value("port", 4269);
        }
        if (j.contains("fields") && j["fields"].is_array()) {
            for (const auto& fj : j["fields"]) {
                ProtocolField f;
                f.fieldId = fj.value("fieldId", "");
                f.oscPath = fj.value("oscPath",  "");
                f.wsKey   = fj.value("wsKey",    "");
                f.enabled = fj.value("enabled",  true);
                if (!f.fieldId.empty())
                    def.fields.push_back(f);
            }
        }
        m_definitions.push_back(def);
        SaveDefinition(def);
        return def.id;
    } catch (...) { return ""; }
}

void ProtocolRegistry::DeleteDefinition(const std::string& id) {
    // Remove file
    std::string path = GetDefsDir() + id + ".json";
    fs::remove(path);

    // Remove from list
    m_definitions.erase(
        std::remove_if(m_definitions.begin(), m_definitions.end(),
                       [&](const ProtocolDefinition& d){ return d.id == id; }),
        m_definitions.end());
}

void ProtocolRegistry::SaveDefinition(const ProtocolDefinition& def) {
    EnsureDirectories();
    std::string path = GetDefsDir() + def.id + ".json";

    json j;
    j["id"]        = def.id;
    j["name"]      = def.name;
    j["transport"] = (def.transport == ProtocolTransport::OSC) ? "osc" : "websocket";
    j["direction"] = (def.direction == ProtocolDirection::Output) ? "output" : "input";
    j["active"]    = def.active;

    j["osc"]["host"]     = def.oscHost;
    j["osc"]["sendPort"] = def.oscSendPort;
    j["osc"]["recvPort"] = def.oscRecvPort;
    j["ws"]["port"]      = def.wssPort;

    json fieldsArr = json::array();
    for (const auto& f : def.fields) {
        json fj;
        fj["fieldId"] = f.fieldId;
        fj["oscPath"] = f.oscPath;
        fj["wsKey"]   = f.wsKey;
        fj["enabled"] = f.enabled;
        fieldsArr.push_back(fj);
    }
    j["fields"] = fieldsArr;

    std::ofstream ofs(path);
    if (ofs) {
        ofs << j.dump(4);
    } else {
        std::cerr << "[ProtocolRegistry] Failed to write " << path << "\n";
    }
}

// ─── Paths ───────────────────────────────────────────────────────────────────

std::string ProtocolRegistry::GetProtocolsDir() {
    const char* base = SDL_GetBasePath();
    std::string dir = base ? std::string(base) : "./";
    return dir + "protocols/";
}

std::string ProtocolRegistry::GetDefsDir() {
    return GetProtocolsDir() + "definitions/";
}

const char* ProtocolRegistry::TransportLabel(ProtocolTransport t) {
    return t == ProtocolTransport::OSC ? "OSC" : "WebSocket";
}

const char* ProtocolRegistry::DirectionLabel(ProtocolDirection d) {
    return d == ProtocolDirection::Output ? "Output (server → client)" : "Input (client → server)";
}

// ─── Private helpers ─────────────────────────────────────────────────────────

void ProtocolRegistry::EnsureDirectories() {
    fs::create_directories(GetDefsDir());
}

void ProtocolRegistry::LoadFieldCatalog() {
    std::string path = GetProtocolsDir() + "input_fields.json";
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        // First run: write out the default catalog so users can extend it.
        WriteDefaultFieldCatalog();
        return;
    }

    try {
        json j = json::parse(ifs);
        // Merge extra output fields from the config file (user may add custom axes)
        if (j.contains("output_fields") && j["output_fields"].is_array()) {
            for (const auto& item : j["output_fields"]) {
                FieldDescriptor fd;
                fd.id             = item.value("id",       "");
                fd.label          = item.value("label",    fd.id);
                fd.category       = item.value("category", "Custom");
                std::string type  = item.value("type",     "analog");
                fd.type           = (type == "digital") ? FieldType::DigitalButton : FieldType::AnalogAxis;
                fd.defaultOscPath = item.value("oscPath",  "/" + fd.id);
                fd.defaultWsKey   = item.value("wsKey",    fd.id);
                fd.isBuiltIn      = false;

                // Only add if not already present (built-ins take precedence by id)
                bool exists = false;
                for (const auto& existing : m_outputFields)
                    if (existing.id == fd.id) { exists = true; break; }
                if (!exists && !fd.id.empty())
                    m_outputFields.push_back(fd);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ProtocolRegistry] Failed to parse input_fields.json: " << e.what() << "\n";
    }
}

void ProtocolRegistry::LoadPresets() {
    m_presets.clear();
    std::string path = GetProtocolsDir() + "presets.json";
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    try {
        json j = json::parse(ifs);
        if (j.contains("presets") && j["presets"].is_array()) {
            for (const auto& item : j["presets"]) {
                FieldPreset p;
                p.name = item.value("name", "Unnamed");
                if (item.contains("fields") && item["fields"].is_array()) {
                    for (const auto& f : item["fields"]) p.fieldIds.push_back(f.get<std::string>());
                }
                m_presets.push_back(p);
            }
        }
    } catch (...) {}
}

void ProtocolRegistry::SavePresets() {
    json j;
    json arr = json::array();
    for (const auto& p : m_presets) {
        json item;
        item["name"] = p.name;
        item["fields"] = p.fieldIds;
        arr.push_back(item);
    }
    j["presets"] = arr;
    std::string path = GetProtocolsDir() + "presets.json";
    std::ofstream ofs(path);
    if (ofs) ofs << j.dump(4);
}

void ProtocolRegistry::LoadDefinitionFiles() {
    m_definitions.clear();
    std::string dir = GetDefsDir();
    if (!fs::exists(dir)) return;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream ifs(entry.path());
        if (!ifs) continue;

        try {
            json j = json::parse(ifs);
            ProtocolDefinition def;
            def.id   = j.value("id",   "");
            def.name = j.value("name", "Unnamed");
            if (def.id.empty()) continue;

            std::string ts = j.value("transport", "osc");
            def.transport = (ts == "websocket") ? ProtocolTransport::WebSocket : ProtocolTransport::OSC;

            std::string dir_s = j.value("direction", "output");
            def.direction = (dir_s == "input") ? ProtocolDirection::Input : ProtocolDirection::Output;

            def.active = j.value("active", false);

            if (j.contains("osc")) {
                def.oscHost     = j["osc"].value("host",     "127.0.0.1");
                def.oscSendPort = j["osc"].value("sendPort", 9066);
                def.oscRecvPort = j["osc"].value("recvPort", 9068);
            }
            if (j.contains("ws")) {
                def.wssPort = j["ws"].value("port", 4269);
            }
            if (j.contains("fields") && j["fields"].is_array()) {
                for (const auto& fj : j["fields"]) {
                    ProtocolField f;
                    f.fieldId = fj.value("fieldId", "");
                    f.oscPath = fj.value("oscPath",  "");
                    f.wsKey   = fj.value("wsKey",    "");
                    f.enabled = fj.value("enabled",  true);
                    if (!f.fieldId.empty())
                        def.fields.push_back(f);
                }
            }
            m_definitions.push_back(def);
        } catch (const std::exception& e) {
            std::cerr << "[ProtocolRegistry] Failed to parse "
                      << entry.path() << ": " << e.what() << "\n";
        }
    }
}

void ProtocolRegistry::WriteDefaultFieldCatalog() {
    EnsureDirectories();

    /*
    std::string path = GetProtocolsDir() + "input_fields.json";
    // Write a commented example so users know the format
    json j;
    j["_comment"] = "Add custom output fields here. Built-in fields are always available.";
    json arr = json::array();
    json ex;
    ex["id"]       = "custom_axis_1";
    ex["label"]    = "Custom Axis 1";
    ex["category"] = "Custom";
    ex["type"]     = "analog";
    ex["oscPath"]  = "/custom/axis1";
    ex["wsKey"]    = "custom_axis1";
    arr.push_back(ex);
    j["output_fields"] = arr;

    std::ofstream ofs(path);
    if (ofs) ofs << j.dump(4);
    */
}

// ─── Built-in field catalogs ─────────────────────────────────────────────────

void ProtocolRegistry::LoadBuiltinCatalog() {
    m_outputFields.clear();
    m_inputFields.clear();

    std::string path = GetProtocolsDir() + "builtin_fields.json";
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        WriteDefaultBuiltinCatalog();
        ifs.open(path);
        if (!ifs.is_open()) return;
    }

    try {
        json j = json::parse(ifs);
        if (j.contains("output_fields") && j["output_fields"].is_array()) {
            for (const auto& item : j["output_fields"]) {
                FieldDescriptor fd;
                fd.id             = item.value("id",       "");
                fd.label          = item.value("label",    fd.id);
                fd.category       = item.value("category", "Custom");
                std::string type  = item.value("type",     "analog");
                fd.type           = (type == "digital") ? FieldType::DigitalButton : FieldType::AnalogAxis;
                fd.defaultOscPath = item.value("oscPath",  "/" + fd.id);
                fd.defaultWsKey   = item.value("wsKey",    fd.id);
                fd.isBuiltIn      = true;
                if (!fd.id.empty()) m_outputFields.push_back(fd);
            }
        }
        if (j.contains("input_fields") && j["input_fields"].is_array()) {
            for (const auto& item : j["input_fields"]) {
                FieldDescriptor fd;
                fd.id             = item.value("id",       "");
                fd.label          = item.value("label",    fd.id);
                fd.category       = item.value("category", "Haptic");
                std::string type  = item.value("type",     "analog");
                fd.type           = (type == "digital") ? FieldType::DigitalButton : FieldType::AnalogAxis;
                fd.defaultOscPath = item.value("oscPath",  "/" + fd.id);
                fd.defaultWsKey   = item.value("wsKey",    fd.id);
                fd.isBuiltIn      = true;
                if (!fd.id.empty()) m_inputFields.push_back(fd);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ProtocolRegistry] Failed to parse builtin_fields.json: " << e.what() << "\n";
    }
}

void ProtocolRegistry::WriteDefaultBuiltinCatalog() {
    json j;
    j["_comment"] = "Default built-in fields. Delete this file to regenerate defaults.";

    json outArr = json::array();
    auto addOut = [&](const char* id, const char* label, const char* cat, FieldType type, const char* oscPath, const char* wsKey) {
        json item;
        item["id"] = id;
        item["label"] = label;
        item["category"] = cat;
        item["type"] = (type == FieldType::DigitalButton) ? "digital" : "analog";
        item["oscPath"] = oscPath;
        item["wsKey"] = wsKey;
        outArr.push_back(item);
    };

    // ── Analog axes ──────────────────────────────────────────────────────────
    addOut("axis_steering",   "Steering / Yaw",  "Analog Axes", FieldType::AnalogAxis, "/input/yaw",        "yaw");
    addOut("axis_throttle",   "Throttle",        "Analog Axes", FieldType::AnalogAxis, "/input/throttle",   "throttle");
    addOut("axis_clutch",     "Clutch",          "Analog Axes", FieldType::AnalogAxis, "/input/clutch",     "clutch");
    addOut("axis_brake",      "Brake",           "Analog Axes", FieldType::AnalogAxis, "/input/brake",      "brake");
    addOut("axis_handbrake",  "Handbrake",       "Analog Axes", FieldType::AnalogAxis, "/input/handbrake",  "handbrake");
    addOut("axis_pitch",      "Pitch",           "Analog Axes", FieldType::AnalogAxis, "/input/pitch",      "pitch");
    addOut("axis_roll",       "Roll",            "Analog Axes", FieldType::AnalogAxis, "/input/roll",       "roll");
    addOut("axis_collective", "Collective",      "Analog Axes", FieldType::AnalogAxis, "/input/collective", "collective");

    // ── Digital: Vehicle ─────────────────────────────────────────────────────
    addOut("btn_gear_up",     "Gear Up",         "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_up",   "gear_up");
    addOut("btn_gear_down",   "Gear Down",       "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_down", "gear_down");
    addOut("btn_neutral",     "Neutral",         "Digital: Vehicle", FieldType::DigitalButton, "/input/neutral",   "neutral");
    addOut("btn_reverse",     "Reverse",         "Digital: Vehicle", FieldType::DigitalButton, "/input/reverse",   "reverse");
    addOut("btn_gear_1",      "1st Gear",        "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_1",    "gear_1");
    addOut("btn_gear_2",      "2nd Gear",        "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_2",    "gear_2");
    addOut("btn_gear_3",      "3rd Gear",        "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_3",    "gear_3");
    addOut("btn_gear_4",      "4th Gear",        "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_4",    "gear_4");
    addOut("btn_gear_5",      "5th Gear",        "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_5",    "gear_5");
    addOut("btn_gear_6",      "6th Gear",        "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_6",    "gear_6");
    addOut("btn_drive_fwd",   "Drive FWD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/fwd",       "fwd");
    addOut("btn_drive_bwd",   "Drive BWD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/bwd",       "bwd");
    addOut("btn_drive_awd",   "Drive AWD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/awd",       "awd");
    addOut("btn_drive_4wd",   "Drive 4WD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/4wd",       "4wd");
    addOut("btn_difflock_f",  "Diff-lock Front", "Digital: Vehicle", FieldType::DigitalButton, "/input/difflock_front", "difflock_front");
    addOut("btn_difflock_b",  "Diff-lock Rear",  "Digital: Vehicle", FieldType::DigitalButton, "/input/difflock_rear",  "difflock_rear");

    // ── Eye Tracking ─────────────────────────────────────────────────────────
    addOut("et_eye_lid_left",   "Eye Lid Left",   "Eye Tracking", FieldType::AnalogAxis, "/avatar/parameters/LeftEyeLid",  "LeftEyeLid");
    addOut("et_eye_lid_right",  "Eye Lid Right",  "Eye Tracking", FieldType::AnalogAxis, "/avatar/parameters/RightEyeLid", "RightEyeLid");
    addOut("et_eye_left_x",     "Eye Left X",     "Eye Tracking", FieldType::AnalogAxis, "/avatar/parameters/EyeLeftX",    "EyeLeftX");
    addOut("et_eye_left_y",     "Eye Left Y",     "Eye Tracking", FieldType::AnalogAxis, "/avatar/parameters/EyeLeftY",    "EyeLeftY");
    addOut("et_eye_right_x",    "Eye Right X",    "Eye Tracking", FieldType::AnalogAxis, "/avatar/parameters/EyeRightX",   "EyeRightX");
    addOut("et_eye_right_y",    "Eye Right Y",    "Eye Tracking", FieldType::AnalogAxis, "/avatar/parameters/EyeRightY",   "EyeRightY");

    // ── Face Tracking ────────────────────────────────────────────────────────
    addOut("ft_jaw_right",          "Jaw Right",            "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/JawRight",           "JawRight");
    addOut("ft_jaw_left",           "Jaw Left",             "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/JawLeft",            "JawLeft");
    addOut("ft_jaw_forward",        "Jaw Forward",          "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/JawForward",         "JawForward");
    addOut("ft_jaw_open",           "Jaw Open",             "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/JawOpen",            "JawOpen");
    addOut("ft_mouth_ape_shape",    "Mouth Ape Shape",      "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthApeShape",      "MouthApeShape");
    addOut("ft_mouth_upper_right",  "Mouth Upper Right",    "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthUpperRight",    "MouthUpperRight");
    addOut("ft_mouth_upper_left",   "Mouth Upper Left",     "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthUpperLeft",     "MouthUpperLeft");
    addOut("ft_mouth_lower_right",  "Mouth Lower Right",    "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthLowerRight",    "MouthLowerRight");
    addOut("ft_mouth_lower_left",   "Mouth Lower Left",     "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthLowerLeft",     "MouthLowerLeft");
    addOut("ft_mouth_upper_overturn","Mouth Upper Overturn","Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthUpperOverturn", "MouthUpperOverturn");
    addOut("ft_mouth_lower_overturn","Mouth Lower Overturn","Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthLowerOverturn", "MouthLowerOverturn");
    addOut("ft_mouth_pout",         "Mouth Pout",           "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthPout",          "MouthPout");
    addOut("ft_mouth_smile_right",  "Mouth Smile Right",    "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthSmileRight",    "MouthSmileRight");
    addOut("ft_mouth_smile_left",   "Mouth Smile Left",     "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthSmileLeft",     "MouthSmileLeft");
    addOut("ft_mouth_sad_right",    "Mouth Sad Right",      "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthSadRight",      "MouthSadRight");
    addOut("ft_mouth_sad_left",     "Mouth Sad Left",       "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthSadLeft",       "MouthSadLeft");
    addOut("ft_cheek_puff_right",   "Cheek Puff Right",     "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/CheekPuffRight",     "CheekPuffRight");
    addOut("ft_cheek_puff_left",    "Cheek Puff Left",      "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/CheekPuffLeft",      "CheekPuffLeft");
    addOut("ft_cheek_suck",         "Cheek Suck",           "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/CheekSuck",          "CheekSuck");
    addOut("ft_mouth_upper_upright","Mouth Upper UpRight",  "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthUpperUpRight",  "MouthUpperUpRight");
    addOut("ft_mouth_upper_upleft", "Mouth Upper UpLeft",   "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthUpperUpLeft",   "MouthUpperUpLeft");
    addOut("ft_mouth_lower_downright","Mouth Lower DownRight","Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthLowerDownRight","MouthLowerDownRight");
    addOut("ft_mouth_lower_downleft","Mouth Lower DownLeft","Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthLowerDownLeft", "MouthLowerDownLeft");
    addOut("ft_mouth_upper_inside", "Mouth Upper Inside",   "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthUpperInside",   "MouthUpperInside");
    addOut("ft_mouth_lower_inside", "Mouth Lower Inside",   "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthLowerInside",   "MouthLowerInside");
    addOut("ft_mouth_lower_overlay","Mouth Lower Overlay",  "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/MouthLowerOverlay",  "MouthLowerOverlay");
    addOut("ft_tongue_longstep1",   "Tongue LongStep1",     "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueLongStep1",    "TongueLongStep1");
    addOut("ft_tongue_left",        "Tongue Left",          "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueLeft",         "TongueLeft");
    addOut("ft_tongue_right",       "Tongue Right",         "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueRight",        "TongueRight");
    addOut("ft_tongue_up",          "Tongue Up",            "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueUp",           "TongueUp");
    addOut("ft_tongue_down",        "Tongue Down",          "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueDown",         "TongueDown");
    addOut("ft_tongue_roll",        "Tongue Roll",          "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueRoll",         "TongueRoll");
    addOut("ft_tongue_longstep2",   "Tongue LongStep2",     "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueLongStep2",    "TongueLongStep2");
    addOut("ft_tongue_upright_morph","Tongue UpRight Morph","Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueUpRightMorph", "TongueUpRightMorph");
    addOut("ft_tongue_upleft_morph", "Tongue UpLeft Morph", "Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueUpLeftMorph",  "TongueUpLeftMorph");
    addOut("ft_tongue_downright_morph","Tongue DownRight Morph","Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueDownRightMorph","TongueDownRightMorph");
    addOut("ft_tongue_downleft_morph","Tongue DownLeft Morph","Face Tracking", FieldType::AnalogAxis, "/avatar/parameters/TongueDownLeftMorph", "TongueDownLeftMorph");

    // ── Digital: Lights ──────────────────────────────────────────────────────
    addOut("btn_lights",      "Lights",          "Digital: Lights", FieldType::DigitalButton, "/input/lights",      "lights");
    addOut("btn_beam",        "Low/High Beam",   "Digital: Lights", FieldType::DigitalButton, "/input/beam",        "beam");
    addOut("btn_parking",     "Parking Light",   "Digital: Lights", FieldType::DigitalButton, "/input/parking",     "parking");
    addOut("btn_fog",         "Fog Light",       "Digital: Lights", FieldType::DigitalButton, "/input/fog",         "fog");
    addOut("btn_turn_left",   "Turn Left",       "Digital: Lights", FieldType::DigitalButton, "/input/turn_left",   "turn_left");
    addOut("btn_turn_right",  "Turn Right",      "Digital: Lights", FieldType::DigitalButton, "/input/turn_right",  "turn_right");
    addOut("btn_hazard",      "Hazard",          "Digital: Lights", FieldType::DigitalButton, "/input/hazard",      "hazard");

    // ── Digital: Other ───────────────────────────────────────────────────────
    addOut("btn_engine",      "Engine",          "Digital: Other",  FieldType::DigitalButton, "/input/engine",      "engine");
    addOut("btn_horn",        "Horn",            "Digital: Other",  FieldType::DigitalButton, "/input/horn",        "horn");
    addOut("btn_cam_switch",  "Camera Switch",   "Digital: Other",  FieldType::DigitalButton, "/input/cam_switch",  "cam_switch");
    addOut("btn_landing_gear","Landing Gear",    "Digital: Other",  FieldType::DigitalButton, "/input/landing_gear","landing_gear");
    addOut("btn_boost",       "Boost / Special", "Digital: Other",  FieldType::DigitalButton, "/input/boost",       "boost");
    addOut("btn_jump",        "Jump",            "Digital: Other",  FieldType::DigitalButton, "/input/jump",        "jump");
    addOut("btn_weapon_main", "Weapon Main",     "Digital: Other",  FieldType::DigitalButton, "/input/weapon_main", "weapon_main");
    addOut("btn_weapon_sec",  "Weapon Secondary","Digital: Other",  FieldType::DigitalButton, "/input/weapon_sec",  "weapon_sec");
    addOut("btn_reload",      "Reload",          "Digital: Other",  FieldType::DigitalButton, "/input/reload",      "reload");

    j["output_fields"] = outArr;

    json inArr = json::array();
    auto addIn = [&](const char* id, const char* label, const char* cat, const char* oscPath, const char* wsKey) {
        json item;
        item["id"] = id;
        item["label"] = label;
        item["category"] = cat;
        item["type"] = "analog";
        item["oscPath"] = oscPath;
        item["wsKey"] = wsKey;
        inArr.push_back(item);
    };

    // ── Haptic feedback fields (received from client) ─────────────────────
    addIn("haptic_rumble",      "Rumble",                  "Haptic", "/haptic/rumble",      "rumble");
    addIn("haptic_constant",    "Constant Force",          "Haptic", "/haptic/constant",    "constant");
    addIn("haptic_periodic",    "Periodic Effect",         "Haptic", "/haptic/periodic",    "periodic");
    addIn("haptic_condition",   "Condition Effect",        "Haptic", "/haptic/condition",   "condition");
    addIn("haptic_gain",        "Global Gain",             "Haptic", "/haptic/gain",        "gain");

    // ── Rumble (simple gamepad) ───────────────────────────────────────────
    addIn("rumble_left",  "Rumble Left Motor",  "Rumble", "/rumble/left",  "rumble_left");
    addIn("rumble_right", "Rumble Right Motor", "Rumble", "/rumble/right", "rumble_right");

    j["input_fields"] = inArr;

    std::string path = GetProtocolsDir() + "builtin_fields.json";
    std::ofstream ofs(path);
    if (ofs) ofs << j.dump(4);
}

// ─── ID generation ───────────────────────────────────────────────────────────

std::string ProtocolRegistry::GenerateId() {
    // Short hex ID based on time + random bits
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::mt19937_64 rng(now);
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t r = dist(rng);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << r;
    return ss.str();
}
