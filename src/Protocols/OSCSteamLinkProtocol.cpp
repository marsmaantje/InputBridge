#include "Protocols/OSCSteamLinkProtocol.h"
#include "Protocols/ProtocolRegistry.h"

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
