#include "ProtocolFieldUtils.h"
#include "Protocols/ProtocolRegistry.h"

namespace InputMapping {

const FieldDescriptor* FindFieldDescriptor(const std::string& id) {
    for (const auto& fd : ProtocolRegistry::GetInstance().GetOutputFields())
        if (fd.id == id) return &fd;
    for (const auto& fd : ProtocolRegistry::GetInstance().GetInputFields())
        if (fd.id == id) return &fd;
    return nullptr;
}

FieldPairs GetEnabledFields(const ProtocolDefinition& def, FieldType type) {
    FieldPairs result;
    for (const auto& pf : def.fields) {
        if (!pf.enabled) continue;
        const auto* fd = FindFieldDescriptor(pf.fieldId);
        if (fd && fd->type == type) result.push_back({&pf, fd});
    }
    return result;
}

} // namespace InputMapping
