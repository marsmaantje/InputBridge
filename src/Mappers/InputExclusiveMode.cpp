#include "InputExclusiveMode.h"
#include "InputExclusiveModeImpl.h"
#ifdef ENABLE_EXCLUSIVE_INPUT
#include "WindowsExclusiveMode.h"
#include "LinuxExclusiveMode.h"
#include "MacOSExclusiveMode.h"
#endif

class DummyExclusiveMode : public InputExclusiveModeImpl {
public:
    void Apply(SDL_Joystick* joystick, bool enabled) override {
        if (enabled) {
            SDL_Log("Exclusive mode not implemented on this platform or disabled");
        }
    }
};

InputExclusiveMode::InputExclusiveMode() {
#ifdef ENABLE_EXCLUSIVE_INPUT
#ifdef _WIN32
    m_Impl = std::make_unique<WindowsExclusiveMode>();
#elif defined(__linux__)
    m_Impl = std::make_unique<LinuxExclusiveMode>();
#elif defined(__APPLE__)
    m_Impl = std::make_unique<MacOSExclusiveMode>();
#else
    m_Impl = std::make_unique<DummyExclusiveMode>();
#endif
#else
    m_Impl = std::make_unique<DummyExclusiveMode>();
#endif
}

InputExclusiveMode::~InputExclusiveMode() = default;

void InputExclusiveMode::SetEnabled(bool enabled) {
    m_Enabled = enabled;
}

bool InputExclusiveMode::IsEnabled() const { return m_Enabled; }

void InputExclusiveMode::Apply(SDL_Joystick* joystick) {
    if (m_Impl) {
        m_Impl->Apply(joystick, m_Enabled);
    }
}