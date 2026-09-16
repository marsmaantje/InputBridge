#include "App/Log.h"
#include "InputExclusiveMode.h"
#include "InputExclusiveModeImpl.h"

static constexpr const char* kTag = "ExclusiveMode";

#ifdef ENABLE_EXCLUSIVE_INPUT
#  ifdef _WIN32
#    include "WindowsExclusiveMode.h"
#  elif defined(__linux__)
#    include "LinuxExclusiveMode.h"
#  elif defined(__APPLE__)
#    include "MacOSExclusiveMode.h"
#  endif
#endif

// --- Dummy fallback ----------------------------------------------------------

class DummyExclusiveMode : public InputExclusiveModeImpl {
public:
    bool HideDevice(SDL_Joystick*) override {
        LOG_INFO(kTag, "Device hide: not implemented on this platform or feature disabled.");
        return false;
    }
    bool UnhideDevice(SDL_Joystick*) override { return true; }
    bool IsAvailable() const override { return false; }
};

// --- InputExclusiveMode -------------------------------------------------------

InputExclusiveMode::InputExclusiveMode() {
#ifdef ENABLE_EXCLUSIVE_INPUT
#  ifdef _WIN32
    m_Impl = std::make_unique<WindowsExclusiveMode>();
#  elif defined(__linux__)
    m_Impl = std::make_unique<LinuxExclusiveMode>();
#  elif defined(__APPLE__)
    m_Impl = std::make_unique<MacOSExclusiveMode>();
#  else
    m_Impl = std::make_unique<DummyExclusiveMode>();
#  endif
#else
    m_Impl = std::make_unique<DummyExclusiveMode>();
#endif
}

InputExclusiveMode::~InputExclusiveMode() = default;

bool InputExclusiveMode::IsAvailable() const {
    return m_Impl && m_Impl->IsAvailable();
}

bool InputExclusiveMode::SetHidden(SDL_Joystick* joystick, bool hidden) {
    if (!m_Impl) return false;
    return hidden ? m_Impl->HideDevice(joystick)
                  : m_Impl->UnhideDevice(joystick);
}

void InputExclusiveMode::SetSteamInputCompatible(bool enabled) {
    if (m_Impl) m_Impl->SetSteamInputCompatible(enabled);
}
