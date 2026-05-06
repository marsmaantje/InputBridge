#include "ButtonBinder.h"
#include "Devices/DeviceManager.h" // For DeviceState

void ButtonBinder::StartBinding(const std::vector<struct DeviceState>& connectedDevices) {
    m_baselineButtonStates.clear();
    for (const auto& dev : connectedDevices) {
        if (!dev.joystick) continue;
        SDL_JoystickID id = dev.instance_id;
        int numButtons = SDL_GetNumJoystickButtons(dev.joystick);
        if (numButtons > 0) {
            std::vector<bool> states(numButtons);
            for (int i = 0; i < numButtons; ++i) {
                states[i] = SDL_GetJoystickButton(dev.joystick, i);
            }
            m_baselineButtonStates[id] = states;
            SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "ButtonBinder: Captured baseline for joystick '%s' (ID: %u).", SDL_GetJoystickName(dev.joystick), id);
        }
    }
    m_isBinding = true;
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "ButtonBinder: Started binding process for all connected joysticks.");
}

std::optional<BoundButtonInfo> ButtonBinder::Update(const std::vector<struct DeviceState>& connectedDevices) {
    if (!m_isBinding) {
        return std::nullopt;
    }

    for (const auto& dev : connectedDevices) {
        if (!dev.joystick) continue;
        SDL_JoystickID id = dev.instance_id;

        auto it = m_baselineButtonStates.find(id);
        if (it == m_baselineButtonStates.end()) {
            // New joystick connected during binding, or joystick was ignored initially.
            // For now, ignore it. Could also re-snapshot all, or just this one.
            continue;
        }

        std::vector<bool>& joystickBaseline = it->second;
        int numButtons = SDL_GetNumJoystickButtons(dev.joystick);

        if (joystickBaseline.size() != numButtons) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "ButtonBinder: Joystick '%s' (ID: %u) button count changed or baseline mismatch. Re-snapshotting.", SDL_GetJoystickName(dev.joystick), id);
            // Re-snapshot this specific joystick's baseline
            joystickBaseline.assign(numButtons, false);
            for (int i = 0; i < numButtons; ++i) joystickBaseline[i] = SDL_GetJoystickButton(dev.joystick, i);
            continue; // Skip this joystick for this update cycle, wait for next.
        }

        for (int i = 0; i < numButtons; ++i) {
            bool currentState = SDL_GetJoystickButton(dev.joystick, i);
            bool wasActiveAtStart = joystickBaseline[i];

            if (currentState && !wasActiveAtStart) {
                m_isBinding = false;
                SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "ButtonBinder: Button %d detected on joystick '%s' (ID: %u).", i, SDL_GetJoystickName(dev.joystick), id);
                return BoundButtonInfo{id, i};
            }
            if (!currentState && wasActiveAtStart) joystickBaseline[i] = false; // Reset baseline if a "stuck" button is released
        }
    }
    return std::nullopt;
}

void ButtonBinder::Cancel() {
    if (m_isBinding) SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "ButtonBinder: Binding process cancelled.");
    m_isBinding = false;
    m_baselineButtonStates.clear();
}

bool ButtonBinder::IsBindingActive() const {
    return m_isBinding;
}
