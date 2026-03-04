#include "Protocols/OSCProjectBabbleProtocol.h"
#include "Protocols/ProtocolRegistry.h"

static void RegisterProjectBabbleFields() {
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

    // ── Face Tracking (Babble / ARKit) ──────────────────────────────────────
    add("pb_jaw_open",       "Jaw Open",          "Babble Face", "/avatar/parameters/JawOpen",       "JawOpen");
    add("pb_jaw_left",       "Jaw Left",          "Babble Face", "/avatar/parameters/JawLeft",       "JawLeft");
    add("pb_jaw_right",      "Jaw Right",         "Babble Face", "/avatar/parameters/JawRight",      "JawRight");
    add("pb_jaw_fwd",        "Jaw Forward",       "Babble Face", "/avatar/parameters/JawFwd",        "JawFwd");
    add("pb_mouth_funnel",   "Mouth Funnel",      "Babble Face", "/avatar/parameters/MouthFunnel",   "MouthFunnel");
    add("pb_mouth_pucker",   "Mouth Pucker",      "Babble Face", "/avatar/parameters/MouthPucker",   "MouthPucker");
    add("pb_mouth_close",    "Mouth Close",       "Babble Face", "/avatar/parameters/MouthClose",    "MouthClose");
    add("pb_mouth_left",     "Mouth Left",        "Babble Face", "/avatar/parameters/MouthLeft",     "MouthLeft");
    add("pb_mouth_right",    "Mouth Right",       "Babble Face", "/avatar/parameters/MouthRight",    "MouthRight");
    add("pb_mouth_smile_l",  "Mouth Smile Left",  "Babble Face", "/avatar/parameters/MouthSmileLeft","MouthSmileLeft");
    add("pb_mouth_smile_r",  "Mouth Smile Right", "Babble Face", "/avatar/parameters/MouthSmileRight","MouthSmileRight");
    add("pb_mouth_frown_l",  "Mouth Frown Left",  "Babble Face", "/avatar/parameters/MouthFrownLeft","MouthFrownLeft");
    add("pb_mouth_frown_r",  "Mouth Frown Right", "Babble Face", "/avatar/parameters/MouthFrownRight","MouthFrownRight");
    add("pb_mouth_dimple_l", "Mouth Dimple Left", "Babble Face", "/avatar/parameters/MouthDimpleLeft","MouthDimpleLeft");
    add("pb_mouth_dimple_r", "Mouth Dimple Right","Babble Face", "/avatar/parameters/MouthDimpleRight","MouthDimpleRight");
    add("pb_mouth_stretch_l","Mouth Stretch Left","Babble Face", "/avatar/parameters/MouthStretchLeft","MouthStretchLeft");
    add("pb_mouth_stretch_r","Mouth Stretch Right","Babble Face", "/avatar/parameters/MouthStretchRight","MouthStretchRight");
    add("pb_mouth_roll_upper","Mouth Roll Upper", "Babble Face", "/avatar/parameters/MouthRollUpper", "MouthRollUpper");
    add("pb_mouth_roll_lower","Mouth Roll Lower", "Babble Face", "/avatar/parameters/MouthRollLower", "MouthRollLower");
    add("pb_mouth_shrug_upper","Mouth Shrug Upper","Babble Face", "/avatar/parameters/MouthShrugUpper","MouthShrugUpper");
    add("pb_mouth_shrug_lower","Mouth Shrug Lower","Babble Face", "/avatar/parameters/MouthShrugLower","MouthShrugLower");
    add("pb_mouth_press_l",  "Mouth Press Left",  "Babble Face", "/avatar/parameters/MouthPressLeft", "MouthPressLeft");
    add("pb_mouth_press_r",  "Mouth Press Right", "Babble Face", "/avatar/parameters/MouthPressRight","MouthPressRight");
    add("pb_mouth_lower_down_l","Mouth Lower Down Left","Babble Face", "/avatar/parameters/MouthLowerDownLeft","MouthLowerDownLeft");
    add("pb_mouth_lower_down_r","Mouth Lower Down Right","Babble Face", "/avatar/parameters/MouthLowerDownRight","MouthLowerDownRight");
    add("pb_mouth_upper_up_l",  "Mouth Upper Up Left",  "Babble Face", "/avatar/parameters/MouthUpperUpLeft",  "MouthUpperUpLeft");
    add("pb_mouth_upper_up_r",  "Mouth Upper Up Right", "Babble Face", "/avatar/parameters/MouthUpperUpRight", "MouthUpperUpRight");
    
    add("pb_tongue_out",     "Tongue Out",        "Babble Face", "/avatar/parameters/TongueOut",     "TongueOut");
    add("pb_tongue_up",      "Tongue Up",         "Babble Face", "/avatar/parameters/TongueUp",      "TongueUp");
    add("pb_tongue_down",    "Tongue Down",       "Babble Face", "/avatar/parameters/TongueDown",    "TongueDown");
    add("pb_tongue_left",    "Tongue Left",       "Babble Face", "/avatar/parameters/TongueLeft",    "TongueLeft");
    add("pb_tongue_right",   "Tongue Right",      "Babble Face", "/avatar/parameters/TongueRight",   "TongueRight");
    add("pb_tongue_roll",    "Tongue Roll",       "Babble Face", "/avatar/parameters/TongueRoll",    "TongueRoll");
    
    add("pb_cheek_puff",     "Cheek Puff",        "Babble Face", "/avatar/parameters/CheekPuff",     "CheekPuff");
    add("pb_cheek_squint_l", "Cheek Squint Left", "Babble Face", "/avatar/parameters/CheekSquintLeft","CheekSquintLeft");
    add("pb_cheek_squint_r", "Cheek Squint Right","Babble Face", "/avatar/parameters/CheekSquintRight","CheekSquintRight");

    add("pb_nose_sneer_l",   "Nose Sneer Left",   "Babble Face", "/avatar/parameters/NoseSneerLeft",   "NoseSneerLeft");
    add("pb_nose_sneer_r",   "Nose Sneer Right",  "Babble Face", "/avatar/parameters/NoseSneerRight",  "NoseSneerRight");
    add("pb_brow_down_l",    "Brow Down Left",    "Babble Face", "/avatar/parameters/BrowDownLeft",    "BrowDownLeft");
    add("pb_brow_down_r",    "Brow Down Right",   "Babble Face", "/avatar/parameters/BrowDownRight",   "BrowDownRight");
    add("pb_brow_inner_up",  "Brow Inner Up",     "Babble Face", "/avatar/parameters/BrowInnerUp",     "BrowInnerUp");
    add("pb_brow_outer_up_l","Brow Outer Up Left","Babble Face", "/avatar/parameters/BrowOuterUpLeft", "BrowOuterUpLeft");
    add("pb_brow_outer_up_r","Brow Outer Up Right","Babble Face", "/avatar/parameters/BrowOuterUpRight","BrowOuterUpRight");
    
    add("pb_eye_blink_l",    "Eye Blink Left",    "Babble Face", "/avatar/parameters/EyeBlinkLeft",    "EyeBlinkLeft");
    add("pb_eye_blink_r",    "Eye Blink Right",   "Babble Face", "/avatar/parameters/EyeBlinkRight",   "EyeBlinkRight");
    add("pb_eye_squint_l",   "Eye Squint Left",   "Babble Face", "/avatar/parameters/EyeSquintLeft",   "EyeSquintLeft");
    add("pb_eye_squint_r",   "Eye Squint Right",  "Babble Face", "/avatar/parameters/EyeSquintRight",  "EyeSquintRight");
    add("pb_eye_wide_l",     "Eye Wide Left",     "Babble Face", "/avatar/parameters/EyeWideLeft",     "EyeWideLeft");
    add("pb_eye_wide_r",     "Eye Wide Right",    "Babble Face", "/avatar/parameters/EyeWideRight",    "EyeWideRight");
    
    add("pb_eye_look_up_l",  "Eye Look Up Left",  "Babble Face", "/avatar/parameters/EyeLookUpLeft",   "EyeLookUpLeft");
    add("pb_eye_look_up_r",  "Eye Look Up Right", "Babble Face", "/avatar/parameters/EyeLookUpRight",  "EyeLookUpRight");
    add("pb_eye_look_down_l","Eye Look Down Left","Babble Face", "/avatar/parameters/EyeLookDownLeft", "EyeLookDownLeft");
    add("pb_eye_look_down_r","Eye Look Down Right","Babble Face", "/avatar/parameters/EyeLookDownRight","EyeLookDownRight");
    add("pb_eye_look_in_l",  "Eye Look In Left",  "Babble Face", "/avatar/parameters/EyeLookInLeft",   "EyeLookInLeft");
    add("pb_eye_look_in_r",  "Eye Look In Right", "Babble Face", "/avatar/parameters/EyeLookInRight",  "EyeLookInRight");
    add("pb_eye_look_out_l", "Eye Look Out Left", "Babble Face", "/avatar/parameters/EyeLookOutLeft",  "EyeLookOutLeft");
    add("pb_eye_look_out_r", "Eye Look Out Right","Babble Face", "/avatar/parameters/EyeLookOutRight", "EyeLookOutRight");

    // ── Eye Tracking (Babble / Generic) ─────────────────────────────────────
    add("pb_eye_left_x",     "Eye Left X",        "Babble Eyes", "/avatar/parameters/EyeLeftX",      "EyeLeftX");
    add("pb_eye_left_y",     "Eye Left Y",        "Babble Eyes", "/avatar/parameters/EyeLeftY",      "EyeLeftY");
    add("pb_eye_right_x",    "Eye Right X",       "Babble Eyes", "/avatar/parameters/EyeRightX",     "EyeRightX");
    add("pb_eye_right_y",    "Eye Right Y",       "Babble Eyes", "/avatar/parameters/EyeRightY",     "EyeRightY");
    add("pb_eye_lid_left",   "Eye Lid Left",      "Babble Eyes", "/avatar/parameters/EyeLidLeft",    "EyeLidLeft");
    add("pb_eye_lid_right",  "Eye Lid Right",     "Babble Eyes", "/avatar/parameters/EyeLidRight",   "EyeLidRight");
}

// Ensure fields are registered at startup
static struct ProjectBabbleFieldRegistrar {
    ProjectBabbleFieldRegistrar() { RegisterProjectBabbleFields(); }
} g_ProjectBabbleFieldRegistrar;

OSCProjectBabbleProtocol::OSCProjectBabbleProtocol() {
    RegisterProjectBabbleFields();
}

std::string OSCProjectBabbleProtocol::getProtocolName() const {
    return "Project Babble OSC";
}

std::string OSCProjectBabbleProtocol::format(const std::string &address, float value) {
    return address + ":" + std::to_string(value);
}

std::string OSCProjectBabbleProtocol::format(const std::string &address, int value) {
    return address + ":" + std::to_string(value);
}

std::string OSCProjectBabbleProtocol::format(const std::string &address, const std::string &value) {
    return address + ":" + value;
}

std::string OSCProjectBabbleProtocol::format_wheel(const std::map<std::string, float>& values) {
    return "";
}

ProtocolDefinition OSCProjectBabbleProtocol::CreateDefaultDefinition() {
    ProtocolDefinition def;
    def.id = "builtin_projectbabble";
    def.name = "Project Babble OSC";
    def.transport = ProtocolTransport::OSC;
    def.direction = ProtocolDirection::Output;
    def.oscHost = "127.0.0.1";
    def.oscSendPort = 9000;
    def.oscRecvPort = 9001;

    for (const auto& fd : ProtocolRegistry::GetInstance().GetOutputFields()) {
        if (fd.category == "Babble Face" || fd.category == "Babble Eyes") {
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
