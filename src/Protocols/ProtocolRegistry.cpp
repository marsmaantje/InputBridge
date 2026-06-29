#include "App/Log.h"
#include "ProtocolRegistry.h"
#include "Utils/XdgDirs.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <random>
#include <SDL3/SDL_filesystem.h>

static constexpr const char* kTag = "ProtocolRegistry";

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
    // Re-save immediately so any stale entries that were blocked by the
    // updated exists-check are purged from input_fields.json on disk.
    SaveFieldCatalog();
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

void ProtocolRegistry::UpdateOutputField(const std::string& originalId, const FieldDescriptor& fd) {
    for (auto& f : m_outputFields) {
        if (f.id == originalId) {
            f = fd;
            SaveFieldCatalog();
            return;
        }
    }
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
        // Skip entries whose ID is already registered in the input catalog —
        // those were misplaced by an older code path and should not be
        // persisted regardless of which category name they carry.
        bool duplicateInOtherCatalog = false;
        for (const auto& other : m_inputFields)
            if (other.id == fd.id) { duplicateInOtherCatalog = true; break; }
        if (duplicateInOtherCatalog) continue;
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



void ProtocolRegistry::DeletePreset(const std::string& name) {
    // Remove the backing template file and the in-memory entry.
    m_presets.erase(
        std::remove_if(m_presets.begin(), m_presets.end(),
                       [&](const FieldPreset& p) {
                           if (p.name != name) return false;
                           std::error_code ec;
                           fs::remove(p.filePath, ec);
                           return true;
                       }),
        m_presets.end());
}

void ProtocolRegistry::SavePreset(const std::string&, const std::vector<std::string>&) {
    // Superseded by SaveTemplate; kept so existing call sites still compile.
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

    // Copy by value BEFORE any mutation of m_definitions. push_back below may
    // reallocate the vector's buffer, which would leave src dangling.
    ProtocolDefinition def = *src;
    src = nullptr; // prevent accidental use after the push_back below

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
    const auto& outFields = m_outputFields;
    const auto& inFields  = m_inputFields;
    for (const auto& f : def->fields) {
        json fj;
        fj["fieldId"] = f.fieldId;
        fj["oscPath"] = f.oscPath;
        fj["wsKey"]   = f.wsKey;
        fj["enabled"] = f.enabled;

        // Embed inline field definition for any field that is not a built-in.
        // This makes exported files self-describing so recipients can import
        // them without pre-populating their own input_fields.json.
        const FieldDescriptor* desc = nullptr;
        for (const auto& fd : outFields) if (fd.id == f.fieldId) { desc = &fd; break; }
        if (!desc) for (const auto& fd : inFields) if (fd.id == f.fieldId) { desc = &fd; break; }
        if (desc && !desc->isBuiltIn) {
            fj["label"]    = desc->label;
            fj["category"] = desc->category;
            fj["type"]     = (desc->type == FieldType::DigitalButton) ? "digital" : "analog";
        } else if (!desc && f.hasInlineDef) {
            // Catalog entry absent (e.g. field not yet persisted) but the
            // ProtocolField still carries its own inline metadata — use it so
            // the exported file stays self-describing.
            fj["label"]    = f.inlineLabel;
            fj["category"] = f.inlineCategory;
            fj["type"]     = (f.inlineType == FieldType::DigitalButton) ? "digital" : "analog";
        }

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
                if (f.fieldId.empty())
                    continue;

                // Read optional inline field definition.
                // These keys are written by ExportDefinition for any field that
                // is not a built-in, so the file is self-describing.
                bool hasInline = fj.contains("label") || fj.contains("category") || fj.contains("type");
                if (hasInline) {
                    f.hasInlineDef   = true;
                    f.inlineLabel    = fj.value("label",    f.fieldId);
                    f.inlineCategory = fj.value("category", "Custom");
                    std::string typeStr = fj.value("type", "digital");
                    f.inlineType = (typeStr == "analog") ? FieldType::AnalogAxis : FieldType::DigitalButton;
                }

                def.fields.push_back(f);

                // Auto-register the field in the catalog when it is unknown so
                // that the editor can display and manipulate it without requiring
                // a matching entry in input_fields.json.
                auto& catalog = (def.direction == ProtocolDirection::Output)
                                 ? m_outputFields : m_inputFields;

                // Check both catalogs: a field may legitimately live in the
                // opposite catalog (e.g. a sensor/button field referenced by an
                // output-direction definition). Registering it a second time in
                // the wrong catalog would create a duplicate "Custom (imported)"
                // entry and break the category grouping in the editor.
                bool knownInCatalog = false;
                for (const auto& desc : m_outputFields)
                    if (desc.id == f.fieldId) { knownInCatalog = true; break; }
                if (!knownInCatalog)
                    for (const auto& desc : m_inputFields)
                        if (desc.id == f.fieldId) { knownInCatalog = true; break; }

                if (!knownInCatalog) {
                    FieldDescriptor fd;
                    fd.id          = f.fieldId;
                    fd.isBuiltIn   = false;
                    if (f.hasInlineDef) {
                        fd.label          = f.inlineLabel;
                        fd.category       = f.inlineCategory;
                        fd.type           = f.inlineType;
                    } else {
                        // No inline metadata — synthesise sensible defaults so
                        // the field at least appears in the editor.
                        fd.label    = f.fieldId;
                        fd.category = "Custom (imported)";
                        fd.type     = FieldType::DigitalButton;
                    }
                    fd.defaultOscPath = f.oscPath.empty() ? ("/" + f.fieldId) : f.oscPath;
                    fd.defaultWsKey   = f.wsKey.empty()   ? f.fieldId         : f.wsKey;
                    catalog.push_back(fd);
                    // Persist so the field survives across restarts.
                    SaveFieldCatalog();
                }
            }
        }
        // Load per-protocol exclusion lists.
        if (j.contains("excluded_fields") && j["excluded_fields"].is_array())
            for (const auto& v : j["excluded_fields"])
                if (v.is_string()) def.excludedFieldIds.push_back(v.get<std::string>());
        if (j.contains("excluded_categories") && j["excluded_categories"].is_array())
            for (const auto& v : j["excluded_categories"])
                if (v.is_string()) def.excludedCategories.push_back(v.get<std::string>());

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

        // Embed inline field definition for non-built-in fields so that the
        // stored definition file is self-describing.
        const FieldDescriptor* desc = nullptr;
        for (const auto& fd : m_outputFields) if (fd.id == f.fieldId) { desc = &fd; break; }
        if (!desc) for (const auto& fd : m_inputFields) if (fd.id == f.fieldId) { desc = &fd; break; }
        if (desc && !desc->isBuiltIn) {
            fj["label"]    = desc->label;
            fj["category"] = desc->category;
            fj["type"]     = (desc->type == FieldType::DigitalButton) ? "digital" : "analog";
        }

        fieldsArr.push_back(fj);
    }
    j["fields"] = fieldsArr;

    // Per-protocol exclusions — only write the keys when non-empty so that
    // the vast majority of definition files stay clean.
    if (!def.excludedFieldIds.empty()) {
        json arr = json::array();
        for (const auto& s : def.excludedFieldIds) arr.push_back(s);
        j["excluded_fields"] = arr;
    }
    if (!def.excludedCategories.empty()) {
        json arr = json::array();
        for (const auto& s : def.excludedCategories) arr.push_back(s);
        j["excluded_categories"] = arr;
    }

    std::ofstream ofs(path);
    if (ofs) {
        ofs << j.dump(4);
    } else {
    LOG_ERROR(kTag, "Failed to write %s", path.c_str());
    }
}

// ─── Paths ───────────────────────────────────────────────────────────────────

std::string ProtocolRegistry::GetProtocolsDir() {
    // Protocol definitions, field catalogs, and templates are user data —
    // they belong in $XDG_DATA_HOME/InputBridge/protocols/ per the XDG Base
    // Directory Specification (fallback: ~/.local/share/InputBridge/protocols/).
    // AppImage Portable Mode remaps $XDG_DATA_HOME automatically when the user
    // places a .home directory next to the .AppImage file.
    return XdgDirs::dataDir() + "protocols/";
}

// Returns the read-only asset directory bundled with the executable.
// Used exclusively for seeding default files into the pref dir on first run.
static std::string GetInstallProtocolsDir() {
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

// Copies seed files from the read-only install directory into the writable
// pref directory, but only when a file is absent (never overwrites).
// This is needed for AppImage / Flatpak where SDL_GetBasePath() returns a
// read-only squashfs mount, while GetProtocolsDir() now points to the pref dir.
static void BootstrapFromInstallDir(const std::string& prefProtocolsDir) {
    const std::string srcDir       = GetInstallProtocolsDir();
    const std::string srcDefs      = srcDir + "definitions/";
    const std::string srcTemplates = srcDir + "templates/";
    const std::string dstDefs      = prefProtocolsDir + "definitions/";
    const std::string dstTemplates = prefProtocolsDir + "templates/";

    // Ensure all destination directories exist.
    fs::create_directories(prefProtocolsDir);
    fs::create_directories(dstDefs);
    fs::create_directories(dstTemplates);

    // Helper: copy every *.json from src -> dst, skipping files that already exist.
    auto seedDir = [](const std::string& src, const std::string& dst) {
        if (!fs::exists(src)) return;
        for (const auto& entry : fs::directory_iterator(src)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            fs::path dstFile = fs::path(dst) / entry.path().filename();
            if (!fs::exists(dstFile)) {
                std::error_code ec;
                fs::copy_file(entry.path(), dstFile,
                              fs::copy_options::skip_existing, ec);
                if (ec)
                    LOG_ERROR(kTag, "Bootstrap copy failed for %s: %s",
                              entry.path().string().c_str(), ec.message().c_str());
            }
        }
    };

    // Seed top-level JSON files (input_fields.json, builtin_fields.json, …).
    seedDir(srcDir, prefProtocolsDir);

    // Seed built-in protocol definitions — these populate the OSC/WS output
    // dropdown via LoadDefinitionFiles(). This was the missing step that caused
    // the dropdown to appear empty on first run under AppImage.
    seedDir(srcDefs, dstDefs);

    // Seed protocol templates (used by the Protocol Editor "new from template").
    seedDir(srcTemplates, dstTemplates);
}

void ProtocolRegistry::EnsureDirectories() {
    const std::string protocolsDir = GetProtocolsDir();
    BootstrapFromInstallDir(protocolsDir);
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

                // Skip if already present in either catalog — a field that
                // belongs in m_inputFields (e.g. a built-in sensor or button)
                // must not be duplicated into m_outputFields regardless of
                // what was written to input_fields.json by an older code path.
                bool exists = false;
                for (const auto& existing : m_outputFields)
                    if (existing.id == fd.id) { exists = true; break; }
                if (!exists)
                    for (const auto& existing : m_inputFields)
                        if (existing.id == fd.id) { exists = true; break; }
                if (!exists && !fd.id.empty())
                    m_outputFields.push_back(fd);
            }
        }
        // Merge extra input fields from the config file (user may add custom haptic/rumble fields)
        if (j.contains("input_fields") && j["input_fields"].is_array()) {
            for (const auto& item : j["input_fields"]) {
                FieldDescriptor fd;
                fd.id             = item.value("id",       "");
                fd.label          = item.value("label",    fd.id);
                fd.category       = item.value("category", "Custom Haptic");
                std::string type  = item.value("type",     "analog"); // Input fields are currently all analog for haptics
                fd.type           = (type == "digital") ? FieldType::DigitalButton : FieldType::AnalogAxis;
                fd.defaultOscPath = item.value("oscPath",  "/" + fd.id);
                fd.defaultWsKey   = item.value("wsKey",    fd.id);
                fd.isBuiltIn      = false;

                // Only add if not already present (built-ins take precedence by id)
                bool exists = false;
                for (const auto& existing : m_inputFields)
                    if (existing.id == fd.id) { exists = true; break; }
                if (!exists && !fd.id.empty())
                    m_inputFields.push_back(fd);
            }
        }
    } catch (const std::exception& e) {
    LOG_ERROR(kTag, "Failed to parse input_fields.json: %s", e.what());
    }
}

void ProtocolRegistry::LoadPresets() {
    // Presets and templates are the same thing: every .json file in the
    // templates/ directory is a saved protocol export.  We scan that folder
    // here so that the "New Protocol" template combo always reflects whatever
    // the user has saved via "Save as Template".
    m_presets.clear();
    std::string templatesDir = GetProtocolsDir() + "templates";
    std::error_code ec;
    if (!fs::is_directory(templatesDir, ec)) return;

    for (const auto& entry : fs::directory_iterator(templatesDir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        try {
            std::ifstream f(entry.path());
            json j;
            f >> j;

            FieldPreset p;
            p.name     = j.value("template_name", entry.path().stem().string());
            p.filePath = entry.path().string();

            for (const auto& fj : j.value("fields", json::array())) {
                FieldPreset::Entry e;
                e.fieldId = fj.value("fieldId", "");
                e.oscPath = fj.value("oscPath", "");
                e.wsKey   = fj.value("wsKey",   "");
                if (!e.fieldId.empty())
                    p.entries.push_back(std::move(e));
            }
            m_presets.push_back(std::move(p));
        } catch (...) {}
    }
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

            // Guard against two files on disk carrying the same "id" (e.g. a
            // stray manual copy of a definition file). Loading both would give
            // the editor list two rows with an identical ImGui id, which trips
            // a "conflicting ID" assertion. Keep whichever file is encountered
            // first and skip the rest.
            if (FindById(def.id)) {
                LOG_ERROR(kTag, "Skipping %s: duplicate protocol id '%s' already loaded",
                          entry.path().string().c_str(), def.id.c_str());
                continue;
            }

            // Guard against two files carrying *different* ids but the same
            // display name (e.g. a leftover auto-generated definition from an
            // older build, sitting alongside its replacement). The id-only
            // check above doesn't catch this, but the editor list row's
            // ImGui id is still derived from "name##id" and renders two
            // visually-identical rows, which is just as confusing and can
            // still trip the same conflicting-ID assertion in edge cases.
            // Keep whichever file is encountered first and skip the rest.
            bool nameAlreadyLoaded = false;
            for (const auto& loaded : m_definitions) {
                if (loaded.name == def.name) { nameAlreadyLoaded = true; break; }
            }
            if (nameAlreadyLoaded) {
                LOG_ERROR(kTag, "Skipping %s: duplicate protocol name '%s' (id '%s') already loaded",
                          entry.path().string().c_str(), def.name.c_str(), def.id.c_str());
                continue;
            }

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
                    if (f.fieldId.empty())
                        continue;

                    bool hasInline = fj.contains("label") || fj.contains("category") || fj.contains("type");
                    if (hasInline) {
                        f.hasInlineDef   = true;
                        f.inlineLabel    = fj.value("label",    f.fieldId);
                        f.inlineCategory = fj.value("category", "Custom");
                        std::string typeStr = fj.value("type", "digital");
                        f.inlineType = (typeStr == "analog") ? FieldType::AnalogAxis : FieldType::DigitalButton;
                    }

                    def.fields.push_back(f);

                    // Auto-register unknown fields so the editor shows them.
                    auto& catalog = (def.direction == ProtocolDirection::Output)
                                     ? m_outputFields : m_inputFields;
                    // Check both catalogs before synthesising an entry; the
                    // field may already be registered on the opposite side.
                    bool known = false;
                    for (const auto& fd : m_outputFields) if (fd.id == f.fieldId) { known = true; break; }
                    if (!known)
                        for (const auto& fd : m_inputFields) if (fd.id == f.fieldId) { known = true; break; }

                    // Sanitise stale entries written by older code paths. If
                    // the field resolves to a real descriptor in either catalog,
                    // evict every duplicate from both catalogs so the correct
                    // category is always shown after a restart.
                    if (known) {
                        const FieldDescriptor* real = nullptr;
                        for (const auto& fd : m_outputFields) if (fd.id == f.fieldId) { real = &fd; break; }
                        if (!real)
                            for (const auto& fd : m_inputFields) if (fd.id == f.fieldId) { real = &fd; break; }

                        // Count total occurrences across both catalogs.
                        int count = 0;
                        for (const auto& fd : m_outputFields) if (fd.id == f.fieldId) count++;
                        for (const auto& fd : m_inputFields)  if (fd.id == f.fieldId) count++;

                        if (count > 1) {
                            // More than one entry for this ID — keep only the
                            // first real (non-stale) one and remove the rest.
                            bool kept = false;
                            auto dedup = [&](std::vector<FieldDescriptor>& cat) {
                                cat.erase(std::remove_if(cat.begin(), cat.end(),
                                    [&](const FieldDescriptor& d) {
                                        if (d.id != f.fieldId) return false;
                                        if (!kept) { kept = true; return false; }
                                        return true;
                                    }), cat.end());
                            };
                            dedup(m_outputFields);
                            dedup(m_inputFields);
                            known = false; // fall through to re-register correctly below
                        }
                    }

                    if (!known) {
                        FieldDescriptor fd;
                        fd.id        = f.fieldId;
                        fd.isBuiltIn = false;
                        if (f.hasInlineDef) {
                            fd.label    = f.inlineLabel;
                            fd.category = f.inlineCategory;
                            fd.type     = f.inlineType;
                        } else {
                            fd.label    = f.fieldId;
                            fd.category = "Custom (imported)";
                            fd.type     = FieldType::DigitalButton;
                        }
                        fd.defaultOscPath = f.oscPath.empty() ? ("/" + f.fieldId) : f.oscPath;
                        fd.defaultWsKey   = f.wsKey.empty()   ? f.fieldId         : f.wsKey;
                        catalog.push_back(fd);
                    }
                }
            }
            // Load per-protocol exclusion lists.
            if (j.contains("excluded_fields") && j["excluded_fields"].is_array())
                for (const auto& v : j["excluded_fields"])
                    if (v.is_string()) def.excludedFieldIds.push_back(v.get<std::string>());
            if (j.contains("excluded_categories") && j["excluded_categories"].is_array())
                for (const auto& v : j["excluded_categories"])
                    if (v.is_string()) def.excludedCategories.push_back(v.get<std::string>());

            m_definitions.push_back(def);
        } catch (const std::exception& e) {
            LOG_ERROR(kTag, "Failed to parse %s: %s",
                      entry.path().string().c_str(), e.what());
        }
    }
}

void ProtocolRegistry::WriteDefaultFieldCatalog() {
    EnsureDirectories();

    std::string path = GetProtocolsDir() + "input_fields.json";
    // Write a commented example so users know the format
    json j;
    j["_comment"] = "Add custom output and input fields here. Built-in fields are always available.";

    json outputArr = json::array();
    json outputEx;
    outputEx["id"]       = "custom_output_axis_1";
    outputEx["label"]    = "Custom Output Axis 1";
    outputEx["category"] = "Custom Output";
    outputEx["type"]     = "analog";
    outputEx["oscPath"]  = "/custom/output/axis1";
    outputEx["wsKey"]    = "custom_output_axis1";
    outputArr.push_back(outputEx);
    j["output_fields"] = outputArr;

    json inputArr = json::array();
    json inputEx;
    inputEx["id"]       = "custom_input_haptic_1";
    inputEx["label"]    = "Custom Input Haptic 1";
    inputEx["category"] = "Custom Haptic Input";
    inputEx["type"]     = "analog"; // Input fields are currently all analog for haptics
    inputEx["oscPath"]  = "/custom/input/haptic1";
    inputEx["wsKey"]    = "custom_input_haptic1";
    inputArr.push_back(inputEx);
    j["input_fields"] = inputArr;

    std::ofstream ofs(path);
    if (ofs) ofs << j.dump(4);
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
    LOG_ERROR(kTag, "Failed to parse builtin_fields.json: %s", e.what());
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
    addOut("btn_gear_7",      "7th Gear",        "Digital: Vehicle", FieldType::DigitalButton, "/input/gear_7",    "gear_7");
    addOut("btn_drive_fwd",   "Drive FWD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/fwd",       "fwd");
    addOut("btn_drive_bwd",   "Drive BWD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/bwd",       "bwd");
    addOut("btn_drive_awd",   "Drive AWD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/awd",       "awd");
    addOut("btn_drive_4wd",   "Drive 4WD",       "Digital: Vehicle", FieldType::DigitalButton, "/input/4wd",       "4wd");
    addOut("btn_difflock_f",  "Diff-lock Front", "Digital: Vehicle", FieldType::DigitalButton, "/input/difflock_front", "difflock_front");
    addOut("btn_difflock_b",  "Diff-lock Rear",  "Digital: Vehicle", FieldType::DigitalButton, "/input/difflock_rear",  "difflock_rear");

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
    
    // ── Sensors: Gyroscope ───────────────────────────────────────────────────
    // Available on DualSense and Steam Controller.  Values normalised to [-1, 1].
    addOut("sensor_gyro_x",   "Gyro X (pitch)",    "Sensors: Gyroscope",      FieldType::AnalogAxis,   "/sensor/gyro/x",         "gyro_x");
    addOut("sensor_gyro_y",   "Gyro Y (yaw)",      "Sensors: Gyroscope",      FieldType::AnalogAxis,   "/sensor/gyro/y",         "gyro_y");
    addOut("sensor_gyro_z",   "Gyro Z (roll)",     "Sensors: Gyroscope",      FieldType::AnalogAxis,   "/sensor/gyro/z",         "gyro_z");

    // ── Sensors: Accelerometer ───────────────────────────────────────────────
    addOut("sensor_accel_x",  "Accel X (lateral)",  "Sensors: Accelerometer", FieldType::AnalogAxis,  "/sensor/accel/x",        "accel_x");
    addOut("sensor_accel_y",  "Accel Y (vertical)", "Sensors: Accelerometer", FieldType::AnalogAxis,  "/sensor/accel/y",        "accel_y");
    addOut("sensor_accel_z",  "Accel Z (fore/aft)", "Sensors: Accelerometer", FieldType::AnalogAxis,  "/sensor/accel/z",        "accel_z");

    // ── Sensors: Touchpad ────────────────────────────────────────────────────
    // x/y centred: left/top=-1, right/bottom=+1.  Pressure: [0, 1].
    addOut("sensor_touch_x",     "Touch X (centered)",  "Sensors: Touchpad", FieldType::AnalogAxis,    "/sensor/touch/x",        "touch_x");
    addOut("sensor_touch_y",     "Touch Y (centered)",  "Sensors: Touchpad", FieldType::AnalogAxis,    "/sensor/touch/y",        "touch_y");
    addOut("sensor_touch_p",     "Touch Pressure",      "Sensors: Touchpad", FieldType::AnalogAxis,    "/sensor/touch/pressure", "touch_pressure");
    addOut("sensor_touch2_x",    "Touch 2 X (centered)","Sensors: Touchpad", FieldType::AnalogAxis,    "/sensor/touch2/x",       "touch2_x");
    addOut("sensor_touch2_y",    "Touch 2 Y (centered)","Sensors: Touchpad", FieldType::AnalogAxis,    "/sensor/touch2/y",       "touch2_y");
    addOut("sensor_touch2_p",    "Touch 2 Pressure",    "Sensors: Touchpad", FieldType::AnalogAxis,    "/sensor/touch2/pressure","touch2_pressure");
    addOut("sensor_touch_active","Touch Active",         "Sensors: Touchpad", FieldType::DigitalButton, "/sensor/touch/active",   "touch_active");


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