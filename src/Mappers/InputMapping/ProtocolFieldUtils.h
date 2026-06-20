#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Small free-function helpers for working with ProtocolDefinition field lists.
// Pulled out of InputMapper because they were duplicated/used by several of
// its pieces (UI, runtime broadcasting, preview) with no shared home.
// ─────────────────────────────────────────────────────────────────────────────

#include "Protocols/ProtocolDefinition.h"
#include <string>
#include <utility>
#include <vector>

namespace InputMapping {

// Looks up a field's catalog descriptor (label, category, type, ...) by id,
// searching both the output and input field catalogs.
const FieldDescriptor* FindFieldDescriptor(const std::string& id);

using FieldPairs = std::vector<std::pair<const ProtocolField*, const FieldDescriptor*>>;

// Returns every enabled field of the given type in `def`, paired with its
// catalog descriptor (fields with no matching descriptor are skipped).
FieldPairs GetEnabledFields(const ProtocolDefinition& def, FieldType type);

} // namespace InputMapping
