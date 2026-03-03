#include "Protocols/OSCSteamLinkProtocol.h"
#include "Protocols/ProtocolRegistry.h"

static void RegisterSteamLinkFields() {
    auto& reg = ProtocolRegistry::GetInstance();
    auto add = [&](const char* id, const char* label, const char* cat, const char* oscPath, const char* wsKey) {
        FieldDescriptor fd;
        fd.id = id;
        fd.label = label;
        fd.category = cat;
        fd.type = FieldType::AnalogAxis;
        fd.defaultOscPath = oscPath;
        fd.defaultWsKey = wsKey;
        fd.isBuiltIn = true;
        reg.AddOutputField(fd);
    };

    // ── Face Tracking ────────────────────────────────────────────────────────
    add("ft_jaw_open",     "Jaw Open",        "Face Tracking", "/avatar/parameters/JawOpen",     "JawOpen");
    add("ft_mouth_pout",   "Mouth Pout",      "Face Tracking", "/avatar/parameters/MouthPout",   "MouthPout");
    add("ft_tongue_out",   "Tongue Out",      "Face Tracking", "/avatar/parameters/TongueOut",   "TongueOut");
    add("ft_cheeks_puff",  "Cheeks Puff",     "Face Tracking", "/avatar/parameters/CheeksPuff",  "CheeksPuff");
    add("ft_mouth_smile",  "Mouth Smile",     "Face Tracking", "/avatar/parameters/MouthSmile",  "MouthSmile");
    add("ft_mouth_frown",  "Mouth Frown",     "Face Tracking", "/avatar/parameters/MouthFrown",  "MouthFrown");

    // ── Eye Tracking ─────────────────────────────────────────────────────────
    add("et_eye_left_x",   "Eye Left X",      "Eye Tracking",  "/avatar/parameters/EyeLeftX",    "EyeLeftX");
    add("et_eye_left_y",   "Eye Left Y",      "Eye Tracking",  "/avatar/parameters/EyeLeftY",    "EyeLeftY");
    add("et_eye_right_x",  "Eye Right X",     "Eye Tracking",  "/avatar/parameters/EyeRightX",   "EyeRightX");
    add("et_eye_right_y",  "Eye Right Y",     "Eye Tracking",  "/avatar/parameters/EyeRightY",   "EyeRightY");
    add("et_eyelid_left",  "Eye Lid Left",    "Eye Tracking",  "/avatar/parameters/EyeLidLeft",  "EyeLidLeft");
    add("et_eyelid_right", "Eye Lid Right",   "Eye Tracking",  "/avatar/parameters/EyeLidRight", "EyeLidRight");
}

// Ensure fields are registered at startup, so they appear in InputMapper
// even if the protocol instance hasn't been created yet.
static struct SteamLinkFieldRegistrar {
    SteamLinkFieldRegistrar() { RegisterSteamLinkFields(); }
} g_SteamLinkFieldRegistrar;

OSCSteamLinkProtocol::OSCSteamLinkProtocol() {
    RegisterSteamLinkFields();
}

std::string OSCSteamLinkProtocol::getProtocolName() const {
    return "SteamLink OSC";
}

std::string OSCSteamLinkProtocol::format(const std::string &address, float value) {
    return address + ":" + std::to_string(value);
}

std::string OSCSteamLinkProtocol::format(const std::string &address, int value) {
    return address + ":" + std::to_string(value);
}

std::string OSCSteamLinkProtocol::format(const std::string &address, const std::string &value) {
    return address + ":" + value;
}

std::string OSCSteamLinkProtocol::format_wheel(const std::map<std::string, float>& values) {
    return "";
}

ProtocolDefinition OSCSteamLinkProtocol::CreateDefaultDefinition() {
    ProtocolDefinition def;
    def.id = "builtin_steamlink";
    def.name = "SteamLink OSC";
    def.transport = ProtocolTransport::OSC;
    def.direction = ProtocolDirection::Output;
    def.oscHost = "127.0.0.1";
    def.oscSendPort = 9000;
    def.oscRecvPort = 9001;

    for (const auto& fd : ProtocolRegistry::GetInstance().GetOutputFields()) {
        if (fd.category == "Face Tracking" || fd.category == "Eye Tracking") {
            ProtocolField pf;
            pf.fieldId = fd.id;
            pf.oscPath = fd.defaultOscPath;
            pf.wsKey = fd.defaultWsKey;
            pf.enabled = true;
            def.fields.push_back(pf);
        }
    }
    return def;
}
