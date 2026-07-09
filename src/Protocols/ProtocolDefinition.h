#pragma once
#include <string>
#include <vector>

// ─── Field descriptors ──────────────────────────────────────────────────────

enum class FieldType {
    AnalogAxis,   // float, range [-1, 1] or [0, 1]
    DigitalButton // bool / int 0|1
};

struct FieldDescriptor {
    std::string id;        // stable identifier, e.g. "axis_steering"
    std::string label;     // human-readable label, e.g. "Steering / Yaw"
    std::string category;  // "Analog", "Digital: Vehicle", "Digital: Lights", etc.
    FieldType   type;
    std::string defaultOscPath;  // suggested OSC address, e.g. "/input/steering"
    std::string defaultWsKey;    // suggested WebSocket JSON key, e.g. "steering"
    bool        isBuiltIn = false;
};

// ─── A single field slot inside a protocol definition ──────────────────────

struct ProtocolField {
    std::string fieldId;   // references FieldDescriptor::id
    std::string oscPath;   // OSC: the actual address to use (may differ from default)
    std::string wsKey;     // WebSocket: JSON key to use
    bool        enabled = true;

    // Optional inline field definition - present only for fields that are not
    // in the built-in catalog.  When set, ImportDefinition auto-registers them
    // as custom FieldDescriptors so they appear in the editor without requiring
    // a pre-existing entry in input_fields.json.
    std::string inlineLabel;     // human-readable label, e.g. "Left Flipper"
    std::string inlineCategory;  // category string, e.g. "Pinball"
    FieldType   inlineType = FieldType::DigitalButton;
    bool        hasInlineDef = false;  // true when the above fields are populated
};

// ─── Protocol direction ─────────────────────────────────────────────────────

enum class ProtocolDirection {
    Send,    // server → client  (send input / sensor data)
    Receive  // client → server  (receive haptic / command data)
};

// ─── Transport type ─────────────────────────────────────────────────────────

enum class ProtocolTransport {
    OSC,
    WebSocket
};

// ─── A complete protocol definition (serialised to a JSON file) ─────────────

struct ProtocolDefinition {
    std::string        id;          // unique UUID / slug (generated on creation)
    std::string        name;        // user-facing name, e.g. "Sim Racing OSC Out"
    ProtocolTransport  transport  = ProtocolTransport::OSC;
    ProtocolDirection  direction  = ProtocolDirection::Send;

    // OSC-specific
    std::string oscHost     = "127.0.0.1";
    int         oscSendPort = 9066;
    int         oscRecvPort = 9068;

    // WebSocket-specific
    int wssPort = 4269;

    // Enabled data fields and their address/key overrides
    std::vector<ProtocolField> fields;

    // Per-protocol visibility exclusions.  Fields / categories listed here are
    // hidden from this protocol's editor tab but remain in the global catalog
    // and are still available to every other protocol.
    std::vector<std::string> excludedFieldIds;
    std::vector<std::string> excludedCategories;

    bool active = false; // whether the server is currently running this definition
};