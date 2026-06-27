#include "InputMapper.h"

std::unique_ptr<InputMapper> InputMapper::s_Instance;

InputMapper& InputMapper::GetInstance() { return *s_Instance; }

void InputMapper::Init(const DeviceManager& deviceManager) {
    if (!s_Instance) s_Instance.reset(new InputMapper(deviceManager));
}

void InputMapper::Shutdown() { s_Instance.reset(); }

InputMapper::InputMapper(const DeviceManager& dm)
    : m_DeviceManager(dm), m_Store(dm), m_UI(dm, m_Store, m_Listener, m_Updater) {}

InputMapper::~InputMapper() {}

bool InputMapper::Update(bool dynamic_rate) {
    auto* profile = m_Store.CurrentProfile();
    if (!profile) return false;

    // Ensure mappings reflect the current hardware session instance IDs.
    m_Store.HandleDeviceConnectionChange();

    const auto* outDef = m_Store.GetActiveOutputDefinition();
    return m_Updater.Update(*profile, outDef, m_Store.SelectedProtocolView(), m_DeviceManager, kGenericOutputs,
                             dynamic_rate);
}

std::string InputMapper::GetOutputPreview() {
    const auto* profile = m_Store.CurrentProfile();
    if (!profile) return "No active profile selected.";

    const auto* outDef = m_Store.GetActiveOutputDefinition();
    return m_Updater.GetOutputPreview(*profile, outDef, m_Store.SelectedProtocolView(), m_DeviceManager,
                                       kGenericOutputs);
}
