#pragma once
#include "ProtocolDefinition.h"
#include <vector>
#include <string>

/**
 * ProtocolRegistry
 *
 * Owns the master catalog of available field descriptors (loaded from
 * "protocols/input_fields.json" next to the executable, with an internal
 * hard-coded fallback) and the list of user-defined protocol definitions
 * (one JSON file per definition inside "protocols/definitions/").
 *
 * Call LoadAll() once at startup and SaveAll() / SaveDefinition() as needed.
 */
struct FieldPreset {
    struct Entry {
        std::string fieldId;
        std::string oscPath;
        std::string wsKey;
    };
    std::string name;
    std::string        filePath;
    std::vector<Entry> entries;
};

class ProtocolRegistry {
public:
    static ProtocolRegistry& GetInstance();

    // ── Lifecycle ────────────────────────────────────────────────────────────
    void LoadAll();   // load field catalog + all definition files
    void SaveAll();   // persist every dirty definition to disk

    // ── Field catalog ────────────────────────────────────────────────────────
    const std::vector<FieldDescriptor>& GetOutputFields() const;  // input data → sent to client
    const std::vector<FieldDescriptor>& GetInputFields()  const;  // haptic/rumble ← received from client
    void ReloadFieldCatalog(); // re-read input_fields.json without touching definitions

    void AddOutputField(const FieldDescriptor& fd);
    void UpdateOutputField(const std::string& originalId, const FieldDescriptor& fd);
    void DeleteOutputField(const std::string& id);
    void SaveFieldCatalog();

    // ── Presets ──────────────────────────────────────────────────────────────
    const std::vector<FieldPreset>& GetPresets() const;
    // Re-scans protocols/templates/ and rebuilds m_presets.  Call after saving
    // a new template so the "New Protocol" combo reflects it immediately.
    void LoadPresets();
    void SavePreset(const std::string& name, const std::vector<std::string>& fieldIds);
    void DeletePreset(const std::string& name);

    // ── Protocol definitions ─────────────────────────────────────────────────
    std::vector<ProtocolDefinition>&       GetDefinitions();
    const std::vector<ProtocolDefinition>& GetDefinitions() const;

    ProtocolDefinition*       FindById(const std::string& id);
    const ProtocolDefinition* FindById(const std::string& id) const;

    /** Create a new blank definition, persist it, return its id. */
    std::string CreateDefinition(const std::string& name,
                                 ProtocolTransport  transport,
                                 ProtocolDirection  direction);

    std::string DuplicateDefinition(const std::string& srcId, const std::string& newName, ProtocolTransport newTransport);

    bool ExportDefinition(const std::string& id, const std::string& path);
    std::string ImportDefinition(const std::string& path);

    /** Remove a definition and delete its file from disk. */
    void DeleteDefinition(const std::string& id);

    /** Persist a single definition (call after editing). */
    void SaveDefinition(const ProtocolDefinition& def);

    // ── Utility ──────────────────────────────────────────────────────────────
    static std::string GetProtocolsDir();   // writable pref dir / "protocols/"
    static std::string GetDefsDir();        // writable pref dir / "protocols/definitions/"

    static const char* TransportLabel(ProtocolTransport t);
    static const char* DirectionLabel(ProtocolDirection d);

private:
    ProtocolRegistry();
    ~ProtocolRegistry() = default;
    ProtocolRegistry(const ProtocolRegistry&) = delete;
    ProtocolRegistry& operator=(const ProtocolRegistry&) = delete;

    void LoadFieldCatalog();
    void LoadDefinitionFiles();

    void EnsureDirectories();
    void WriteDefaultFieldCatalog();
    void LoadBuiltinCatalog();
    void WriteDefaultBuiltinCatalog();

    static std::string GenerateId();

    std::vector<FieldDescriptor> m_outputFields; // data the server can send out
    std::vector<FieldDescriptor> m_inputFields;  // haptic / rumble data the server can receive
    std::vector<ProtocolDefinition> m_definitions;
    std::vector<FieldPreset> m_presets;
};