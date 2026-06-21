#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame output computation: reads the active profile's mappings against
// live hardware state, then either broadcasts the result over OSC/WebSocket
// (Update) or formats it as human-readable text (GetOutputPreview).
// ─────────────────────────────────────────────────────────────────────────────

#include "MappingTypes.h"
#include <SDL3/SDL.h>
#include <map>
#include <string>
#include <vector>

class DeviceManager;
struct ProtocolDefinition;

namespace InputMapping {

class OutputRuntimeUpdater {
  public:
    // Computes the current frame's output and broadcasts it over OSC/
    // WebSocket, subject to dynamic-rate change-detection (resend at most
    // every frame if `dynamic_rate` is false, otherwise only on change or
    // at least every 500ms as a heartbeat). Returns true if a broadcast was
    // actually sent this call.
    bool Update(MappingProfile& profile, const ProtocolDefinition* outDef, int selectedProtocolView,
                const DeviceManager& dm, const std::vector<std::string>& genericOutputs, bool dynamic_rate);

    // Builds a human-readable preview of what would currently be sent. Pure
    // (does not affect Update's change-detection state or any mapping's
    // toggle/edge state).
    std::string GetOutputPreview(const MappingProfile& profile, const ProtocolDefinition* outDef,
                                  int selectedProtocolView, const DeviceManager& dm,
                                  const std::vector<std::string>& genericOutputs) const;

  private:
    std::map<std::string, float> m_LastOutputValues;
    Uint64 m_LastBroadcastTime = 0;
};

} // namespace InputMapping
