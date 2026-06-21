#include "OutputRuntimeUpdater.h"

#include "Devices/DeviceManager.h"
#include "InputSignalProcessing.h"
#include "Network/OSCServer.h"
#include "Network/WebSocketServer.h"
#include "ProtocolFieldUtils.h"
#include "Protocols/OSCProjectBabbleProtocol.h"
#include "Protocols/OSCSteamLinkProtocol.h"
#include "Protocols/ProtocolManager.h"
#include "Protocols/ProtocolRegistry.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace InputMapping {

namespace {

// ── Shared "is this mapping currently active" predicates ───────────────────
// Used to read the live state of a button/hat/sensor-backed mapping. Works
// for both ButtonToAnalogMapping and ButtonToDigitalMapping since they share
// ───────────────────────────────────────────────────────────────────────────

template <typename ButtonMapping>
bool IsButtonMappingPressed(const ButtonMapping& m, const DeviceManager& dm) {
    if (m.button_index != -1 || m.hat_index != -1) { // sentinel (< -1) is a gamepad paddle button
        SDL_Joystick* j = FindJoystick(m.instance_id, dm);
        if (!j) return false;
        if (m.button_index != -1) return ReadButtonState(m.instance_id, m.button_index, dm);
        return (SDL_GetJoystickHat(j, m.hat_index) & m.hat_mask) != 0;
    }
    if (m.sensor_channel != InputSource::SensorChannel::None) {
        InputSource tmp;
        tmp.instance_id = m.instance_id;
        tmp.sensorChannel = m.sensor_channel;
        return ProcessSensor(tmp, dm) > 0.5f;
    }
    return false;
}

bool IsAnalogToDigitalActive(const AnalogToDigitalMapping& am, const DeviceManager& dm) {
    if (am.source.instance_id == 0 && am.source.axisIndex == -1 &&
        am.source.sensorChannel == InputSource::SensorChannel::None)
        return false;
    float val = ReadInputSourceValue(am.source, dm);
    return am.invert_threshold ? (val < am.threshold) : (val >= am.threshold);
}

// ── Analog values ───────────────────────────────────────────────────────────

void ApplyButtonOverrides(const MappingProfile& profile, const DeviceManager& dm,
                           std::map<std::string, float>& analogValues) {
    for (const auto& bm : profile.buttonMappings) {
        if (bm.instance_id == 0 || bm.target_output_name.empty()) continue;
        if (IsButtonMappingPressed(bm, dm)) analogValues[bm.target_output_name] = bm.on_value;
    }
}

void ApplyChannelMixes(const MappingProfile& profile, const DeviceManager& dm,
                        std::map<std::string, float>& analogValues) {
    for (const auto& mix : profile.channelMixes) {
        if (mix.target_field_id.empty() || mix.sources.empty()) continue;
        float sum = 0.f;
        for (const auto& ms : mix.sources) sum += ReadInputSourceValue(ms.source, dm) * ms.weight;
        if (mix.clamp_output) sum = std::clamp(sum, -1.f, 1.f);
        analogValues[mix.target_field_id] = sum;
    }
}

// Computes every analog output value for the current frame: the base axis
// mapping, then button-to-analog overrides, then channel mixes (in that
// priority order, matching the original Update()/GetOutputPreview()). This
// is identical regardless of whether the result is about to be broadcast or
// previewed, so both call into this one function.
std::map<std::string, float> ComputeAnalogValues(const MappingProfile& profile, const ProtocolDefinition* outDef,
                                                   const DeviceManager& dm,
                                                   const std::vector<std::string>& genericOutputs) {
    std::map<std::string, float> analogValues;
    if (outDef) {
        for (auto& [pf, fd] : GetEnabledFields(*outDef, FieldType::AnalogAxis)) {
            auto it = profile.outputToInput.find(pf->fieldId);
            analogValues[pf->fieldId] = it != profile.outputToInput.end() ? ReadInputSourceValue(it->second, dm) : 0.f;
        }
    } else {
        for (const auto& name : genericOutputs) analogValues[name] = 0.f;
        for (const auto& [k, src] : profile.outputToInput) analogValues[k] = ReadInputSourceValue(src, dm);
    }
    ApplyButtonOverrides(profile, dm, analogValues);
    ApplyChannelMixes(profile, dm, analogValues);
    return analogValues;
}

// ── Digital values ──────────────────────────────────────────────────────────

// Mutates last_physical_state / last_state and digitalToggleStates: rising
// edges on a Toggle/SetOn/SetOff mapping flip the field's persistent toggle
// state. Runs unconditionally — processes profile mappings, not protocol
// fields, so must NOT be gated on outDef. GetActiveOutputDefinition() only
// returns the definition for the currently-viewed UI tab; gating here would
// mean the OSC state stops updating whenever the WebSocket tab is active (or
// vice-versa). Called once per Update() tick; never called from preview, so
// previewing never perturbs toggle/edge state.
void UpdateDigitalEdgeState(MappingProfile& profile, const DeviceManager& dm) {
    for (auto& dm_ : profile.digitalMappings) {
        if (dm_.instance_id == 0 || dm_.target_field_id.empty()) continue;
        bool pressed = IsButtonMappingPressed(dm_, dm);

        if (dm_.mode != ButtonToDigitalMapping::Mode::Momentary && pressed && !dm_.last_physical_state) {
            auto it = profile.digitalToggleStates.find(dm_.target_field_id);
            bool currentState = (it != profile.digitalToggleStates.end()) ? it->second : false;
            switch (dm_.mode) {
                case ButtonToDigitalMapping::Mode::Toggle: profile.digitalToggleStates[dm_.target_field_id] = !currentState; break;
                case ButtonToDigitalMapping::Mode::SetOn:  profile.digitalToggleStates[dm_.target_field_id] = true; break;
                case ButtonToDigitalMapping::Mode::SetOff: profile.digitalToggleStates[dm_.target_field_id] = false; break;
                default: break;
            }
        }
        dm_.last_physical_state = pressed;
    }

    for (auto& am : profile.analogToDigitalMappings) {
        if (am.target_field_id.empty()) continue;
        if (am.source.instance_id == 0 && am.source.axisIndex == -1 &&
            am.source.sensorChannel == InputSource::SensorChannel::None)
            continue;

        bool pressed = IsAnalogToDigitalActive(am, dm);
        if (am.mode != AnalogToDigitalMapping::Mode::Momentary && pressed && !am.last_state) {
            auto it = profile.digitalToggleStates.find(am.target_field_id);
            bool cur = (it != profile.digitalToggleStates.end()) ? it->second : false;
            switch (am.mode) {
                case AnalogToDigitalMapping::Mode::Toggle: profile.digitalToggleStates[am.target_field_id] = !cur; break;
                case AnalogToDigitalMapping::Mode::SetOn:  profile.digitalToggleStates[am.target_field_id] = true; break;
                case AnalogToDigitalMapping::Mode::SetOff: profile.digitalToggleStates[am.target_field_id] = false; break;
                default: break;
            }
        }
        am.last_state = pressed;
    }
}

// True if `fieldId` has any mapping configured in a state-managing mode
// (Toggle/SetOn/SetOff) — meaning its current value should come from
// digitalToggleStates rather than defaulting to false.
bool HasStateManagedMapping(const MappingProfile& profile, const std::string& fieldId) {
    for (const auto& dm : profile.digitalMappings)
        if (dm.target_field_id == fieldId && dm.mode != ButtonToDigitalMapping::Mode::Momentary) return true;
    for (const auto& am : profile.analogToDigitalMappings)
        if (am.target_field_id == fieldId && am.mode != AnalogToDigitalMapping::Mode::Momentary) return true;
    return false;
}

// Resolves one digital field's current boolean value: the toggle state (if
// any mapping manages it via Toggle/SetOn/SetOff) OR'd with any Momentary
// mapping currently being held. Read-only — does not mutate edge state, so
// it's safe to call from both Update() (after UpdateDigitalEdgeState) and
// GetOutputPreview() (standalone): within a single synchronous frame, a
// fresh press-read here gives the same answer last_physical_state/last_state
// would have just been set to.
bool ResolveSingleDigitalField(const MappingProfile& profile, const std::string& fieldId, const DeviceManager& dm) {
    bool value = false;
    if (HasStateManagedMapping(profile, fieldId)) {
        auto it = profile.digitalToggleStates.find(fieldId);
        if (it != profile.digitalToggleStates.end()) value = it->second;
    }
    for (const auto& mapping : profile.digitalMappings) {
        if (mapping.target_field_id != fieldId || mapping.mode != ButtonToDigitalMapping::Mode::Momentary) continue;
        if (mapping.instance_id != 0 && IsButtonMappingPressed(mapping, dm)) { value = true; break; }
    }
    if (!value) {
        for (const auto& am : profile.analogToDigitalMappings) {
            if (am.target_field_id != fieldId || am.mode != AnalogToDigitalMapping::Mode::Momentary) continue;
            if (IsAnalogToDigitalActive(am, dm)) { value = true; break; }
        }
    }
    return value;
}

// Resolves every enabled digital field across `defs` (deduplicated by field
// id, first definition wins). Update() passes every protocol definition that
// could currently be live (OSC + WebSocket + the active tab's) so background
// state-tracking keeps working for whichever protocol isn't being viewed;
// GetOutputPreview() passes just the tab being previewed.
std::map<std::string, bool> ResolveDigitalValues(const MappingProfile& profile,
                                                   const std::vector<const ProtocolDefinition*>& defs,
                                                   const DeviceManager& dm) {
    std::map<std::string, bool> digitalValues;
    std::set<std::string> resolvedFieldIds;
    for (const auto* def : defs) {
        if (!def) continue;
        for (auto& [pf, fd] : GetEnabledFields(*def, FieldType::DigitalButton)) {
            if (!resolvedFieldIds.insert(pf->fieldId).second) continue; // already resolved
            digitalValues[pf->fieldId] = ResolveSingleDigitalField(profile, pf->fieldId, dm);
        }
    }
    return digitalValues;
}

// ── "Does this field have any device input assigned at all" ────────────────
// Determines whether a field should be sent/previewed: a field with no
// device input bound is silently skipped rather than sent as 0/false.

bool HasAnalogFieldSource(const MappingProfile& profile, const std::string& fieldId) {
    auto it = profile.outputToInput.find(fieldId);
    if (it != profile.outputToInput.end() && it->second.instance_id != 0) return true;
    for (const auto& bm : profile.buttonMappings)
        if (bm.instance_id != 0 && bm.target_output_name == fieldId) return true;
    for (const auto& mix : profile.channelMixes)
        if (!mix.sources.empty() && mix.target_field_id == fieldId) return true;
    return false;
}

bool HasDigitalFieldSource(const MappingProfile& profile, const std::string& fieldId) {
    for (const auto& dm : profile.digitalMappings)
        if (dm.instance_id != 0 && dm.target_field_id == fieldId) return true;
    for (const auto& am : profile.analogToDigitalMappings)
        if (am.source.instance_id != 0 && am.target_field_id == fieldId) return true;
    return false;
}

// ── Live OSC output-definition resolution ──────────────────────────────────
// Resolves the OSC server's *actual current* output definition: its
// explicitly-selected id if set, falling back to the SteamLink/Project
// Babble built-ins if the server is running one of those legacy protocol
// names. This one block was duplicated three times across the original
// Update() and GetOutputPreview() (the "oscDef2" digital-field-enumeration
// resolution, the "oscDef" broadcast resolution, and the preview's mirror of
// it) — callers compose it differently (see BroadcastValues and
// AppendOscPreview) but all three start from this same resolution.
const ProtocolDefinition* ResolveLiveOscOutputDefinition() {
    auto& osc = OSCServer::GetInstance();
    std::string oscId = osc.GetOutputDefinitionId();
    if (!oscId.empty()) return ProtocolRegistry::GetInstance().FindById(oscId);

    std::string protocol = osc.GetProtocol();
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

// ── Broadcasting (Update) ───────────────────────────────────────────────────

void BroadcastViaDefinition(const MappingProfile& profile, const ProtocolDefinition& outDef,
                             int selectedProtocolView, const std::map<std::string, float>& analogValues,
                             const std::map<std::string, bool>& digitalValues) {
    auto& osc = OSCServer::GetInstance();
#ifdef ENABLE_WEBSOCKETS
    auto& ws = WebSocketServer::GetInstance();
#endif

    const ProtocolDefinition* oscDef = (selectedProtocolView == 0) ? &outDef : ResolveLiveOscOutputDefinition();
#ifdef ENABLE_WEBSOCKETS
    const auto* wsDef = ProtocolRegistry::GetInstance().FindById(ws.GetOutputDefinitionId());
#endif

    for (auto& [pf, fd] : GetEnabledFields(outDef, FieldType::AnalogAxis)) {
        if (!HasAnalogFieldSource(profile, pf->fieldId)) continue;
        auto vIt = analogValues.find(pf->fieldId);
        float val = vIt != analogValues.end() ? vIt->second : 0.f;
        if (osc.IsRunning() && oscDef)
            for (const auto& op : oscDef->fields)
                if (op.fieldId == pf->fieldId && op.enabled) { osc.Send(op.oscPath.c_str(), "f", val); break; }
#ifdef ENABLE_WEBSOCKETS
        if (ws.IsRunning() && wsDef)
            for (const auto& wp : wsDef->fields)
                if (wp.fieldId == pf->fieldId && wp.enabled) { ws.Broadcast(wp.wsKey, val); break; }
#endif
    }

    for (auto& [pf, fd] : GetEnabledFields(outDef, FieldType::DigitalButton)) {
        if (!HasDigitalFieldSource(profile, pf->fieldId)) continue;
        auto vIt = digitalValues.find(pf->fieldId);
        int val = (vIt != digitalValues.end() && vIt->second) ? 1 : 0;
        if (osc.IsRunning() && oscDef)
            for (const auto& op : oscDef->fields)
                if (op.fieldId == pf->fieldId && op.enabled) { osc.Send(op.oscPath.c_str(), val ? "T" : "F"); break; }
#ifdef ENABLE_WEBSOCKETS
        if (ws.IsRunning() && wsDef)
            for (const auto& wp : wsDef->fields)
                if (wp.fieldId == pf->fieldId && wp.enabled) { ws.Broadcast(wp.wsKey, val); break; }
#endif
    }
}

void BroadcastLegacyWheel(const std::map<std::string, float>& analogValues) {
    auto valueOf = [&](const char* k) {
        auto it = analogValues.find(k);
        return it != analogValues.end() ? it->second : 0.f;
    };
    float s = valueOf("Steering"), t = valueOf("Throttle"), b = valueOf("Brake");
    float pi = valueOf("Pitch"), ro = valueOf("Roll");

#ifdef ENABLE_WEBSOCKETS
    auto& ws = WebSocketServer::GetInstance();
    if (ws.IsRunning()) ws.Broadcast_wheel(s, b, t, pi, ro);
#endif
    auto& osc = OSCServer::GetInstance();
    if (osc.IsRunning()) osc.SendWheel(s, b, t, pi, ro);
}

void BroadcastValues(const MappingProfile& profile, const ProtocolDefinition* outDef, int selectedProtocolView,
                      const std::map<std::string, float>& analogValues,
                      const std::map<std::string, bool>& digitalValues) {
    if (outDef) BroadcastViaDefinition(profile, *outDef, selectedProtocolView, analogValues, digitalValues);
    else BroadcastLegacyWheel(analogValues);
}

// ── Preview text ─────────────────────────────────────────────────────────────

void AppendOscPreview(std::stringstream& ss, const MappingProfile& profile, const ProtocolDefinition& outDef,
                       const std::map<std::string, float>& analogValues,
                       const std::map<std::string, bool>& digitalValues) {
    auto& osc = OSCServer::GetInstance();

    // Mirror Update()'s oscDef resolution so the preview only shows fields
    // that the OSC server will actually transmit.
    const ProtocolDefinition* oscDef = &outDef;
    if (const auto* resolved = ResolveLiveOscOutputDefinition()) oscDef = resolved;

    ss << "[OSC Output]";
    if (!osc.IsRunning()) ss << " (Stopped)";
    else if (oscDef != &outDef) ss << " (Separate OSC Definition: " << oscDef->name << ")";
    ss << "\n";

    int sentCount = 0;
    for (auto& [pf, fd] : GetEnabledFields(outDef, FieldType::AnalogAxis)) {
        if (!HasAnalogFieldSource(profile, pf->fieldId)) continue;
        const ProtocolField* op = nullptr;
        for (const auto& f : oscDef->fields)
            if (f.fieldId == pf->fieldId && f.enabled) { op = &f; break; }
        if (!op) continue;
        ss << "  " << op->oscPath << " " << std::fixed << std::setprecision(4) << analogValues.at(pf->fieldId) << "\n";
        ++sentCount;
    }
    for (auto& [pf, fd] : GetEnabledFields(outDef, FieldType::DigitalButton)) {
        if (!HasDigitalFieldSource(profile, pf->fieldId)) continue;
        const ProtocolField* op = nullptr;
        for (const auto& f : oscDef->fields)
            if (f.fieldId == pf->fieldId && f.enabled) { op = &f; break; }
        if (!op) continue;
        ss << "  " << op->oscPath << " " << (digitalValues.at(pf->fieldId) ? "T" : "F") << "\n";
        ++sentCount;
    }
    if (sentCount == 0) ss << "  (No mapped fields — assign device inputs on this page)\n";
}

void AppendWsPreview(std::stringstream& ss, const MappingProfile& profile, const ProtocolDefinition& outDef,
                      const std::map<std::string, float>& analogValues,
                      const std::map<std::string, bool>& digitalValues) {
#ifdef ENABLE_WEBSOCKETS
    auto& ws = WebSocketServer::GetInstance();

    // Mirror Update()'s wsDef resolution.
    const ProtocolDefinition* wsDef = nullptr;
    std::string wsId = ws.GetOutputDefinitionId();
    if (!wsId.empty()) wsDef = ProtocolRegistry::GetInstance().FindById(wsId);

    ss << "[WebSocket Output]";
    if (!ws.IsRunning()) ss << " (Stopped)";
    else if (!wsDef) ss << " (No WS Definition Selected)";
    ss << "\n";

    std::string protoName = ws.GetProtocol();
    auto protocol = ProtocolManager::GetInstance().GetProtocol(protoName);
    if (protocol && wsDef) {
        int sentCount = 0;
        for (auto& [pf, fd] : GetEnabledFields(outDef, FieldType::AnalogAxis)) {
            if (!HasAnalogFieldSource(profile, pf->fieldId)) continue;
            const ProtocolField* wp = nullptr;
            for (const auto& f : wsDef->fields)
                if (f.fieldId == pf->fieldId && f.enabled) { wp = &f; break; }
            if (!wp) continue;
            ss << "  " << protocol->format(wp->wsKey, analogValues.at(pf->fieldId)) << "\n";
            ++sentCount;
        }
        for (auto& [pf, fd] : GetEnabledFields(outDef, FieldType::DigitalButton)) {
            if (!HasDigitalFieldSource(profile, pf->fieldId)) continue;
            const ProtocolField* wp = nullptr;
            for (const auto& f : wsDef->fields)
                if (f.fieldId == pf->fieldId && f.enabled) { wp = &f; break; }
            if (!wp) continue;
            ss << "  " << protocol->format(wp->wsKey, digitalValues.at(pf->fieldId) ? 1 : 0) << "\n";
            ++sentCount;
        }
        if (sentCount == 0) ss << "  (No mapped fields — assign device inputs on this page)\n";
    } else if (!protocol) {
        ss << "  (Unknown Protocol)\n";
    } else {
        ss << "  (No WS definition — select one in the Network tab)\n";
    }
#else
    (void)profile; (void)outDef; (void)analogValues; (void)digitalValues;
    ss << "WebSockets disabled.\n";
#endif
}

void AppendLegacyPreview(std::stringstream& ss, const std::map<std::string, float>& analogValues,
                          int selectedProtocolView) {
    ss << "Legacy Mode (No Output Definition Selected)\n\n";

    auto valueOf = [&](const char* k) {
        auto it = analogValues.find(k);
        return it != analogValues.end() ? it->second : 0.f;
    };
    float s = valueOf("Steering"), t = valueOf("Throttle"), b = valueOf("Brake");
    float pi = valueOf("Pitch"), ro = valueOf("Roll");

    std::map<std::string, float> wheelValues;
    if (analogValues.count("Steering")) wheelValues["wheel"] = s;
    if (analogValues.count("Throttle")) wheelValues["throttle"] = t;
    if (analogValues.count("Brake")) wheelValues["brake"] = b;
    if (analogValues.count("Pitch")) wheelValues["pitch"] = pi;
    if (analogValues.count("Roll")) wheelValues["roll"] = ro;

    auto& osc = OSCServer::GetInstance();
    if (selectedProtocolView == 0) {
        ss << "[OSC Output]";
        if (!osc.IsRunning()) ss << " (Stopped)";
        ss << "\n";
        ss << "  /wheel/steer " << std::fixed << std::setprecision(4) << s << "\n";
        ss << "  /wheel/throttle " << t << "\n";
        ss << "  /wheel/brake " << b << "\n";
        ss << "  /wheel/pitch " << pi << "\n";
        ss << "  /wheel/roll " << ro << "\n";
        ss << "  (Buttons not previewed in legacy mode)\n\n";
        return;
    }
#ifdef ENABLE_WEBSOCKETS
    auto& ws = WebSocketServer::GetInstance();
    ss << "[WebSocket Output]";
    if (!ws.IsRunning()) ss << " (Stopped)";
    ss << "\n";
    std::string protoName = ws.GetProtocol();
    auto protocol = ProtocolManager::GetInstance().GetProtocol(protoName);
    if (protocol) ss << "  " << protocol->format_wheel(wheelValues) << "\n";
    else ss << "  (Unknown Protocol)\n";
#else
    ss << "WebSockets disabled.\n";
#endif
}

} // namespace

bool OutputRuntimeUpdater::Update(MappingProfile& profile, const ProtocolDefinition* outDef,
                                   int selectedProtocolView, const DeviceManager& dm,
                                   const std::vector<std::string>& genericOutputs, bool dynamic_rate) {
    auto analogValues = ComputeAnalogValues(profile, outDef, dm, genericOutputs);

    UpdateDigitalEdgeState(profile, dm);

    std::map<std::string, bool> digitalValues;
    if (outDef) {
        // Enumerate fields from BOTH the OSC and WebSocket protocol definitions,
        // not just outDef (which only reflects the currently-viewed UI tab). If
        // OSC and WS use different protocol definitions, fields belonging to the
        // non-active tab would otherwise never be populated here and therefore
        // never transmitted.
        const ProtocolDefinition* oscDef2 = ResolveLiveOscOutputDefinition();
#ifdef ENABLE_WEBSOCKETS
        const ProtocolDefinition* wsDef2 =
            ProtocolRegistry::GetInstance().FindById(WebSocketServer::GetInstance().GetOutputDefinitionId());
#else
        const ProtocolDefinition* wsDef2 = nullptr;
#endif
        // Also cover outDef in case it differs from both (e.g. a third custom view).
        digitalValues = ResolveDigitalValues(profile, {oscDef2, wsDef2, outDef}, dm);
    }

    // Change detection snapshot.
    std::map<std::string, float> snapshot;
    for (const auto& [k, v] : analogValues) snapshot[k] = v;
    for (const auto& [k, v] : digitalValues) snapshot["__d_" + k] = v ? 1.f : 0.f;

    bool should_send = true;
    if (dynamic_rate) {
        Uint64 now = SDL_GetTicks();
        should_send = (snapshot != m_LastOutputValues) || (now - m_LastBroadcastTime) >= 500;
    }
    if (!should_send) return false;

    BroadcastValues(profile, outDef, selectedProtocolView, analogValues, digitalValues);

    m_LastOutputValues = snapshot;
    m_LastBroadcastTime = SDL_GetTicks();
    return true;
}

std::string OutputRuntimeUpdater::GetOutputPreview(const MappingProfile& profile, const ProtocolDefinition* outDef,
                                                    int selectedProtocolView, const DeviceManager& dm,
                                                    const std::vector<std::string>& genericOutputs) const {
    std::stringstream ss;
    auto analogValues = ComputeAnalogValues(profile, outDef, dm, genericOutputs);

    if (!outDef) {
        AppendLegacyPreview(ss, analogValues, selectedProtocolView);
        return ss.str();
    }

    auto digitalValues = ResolveDigitalValues(profile, {outDef}, dm);

    ss << "Active Definition: " << outDef->name << "\n\n";
    if (selectedProtocolView == 0) AppendOscPreview(ss, profile, *outDef, analogValues, digitalValues);
    else AppendWsPreview(ss, profile, *outDef, analogValues, digitalValues);
    return ss.str();
}

} // namespace InputMapping
