#include "App/Log.h"
#include "ButtonBinder.h"
#include "Devices/DeviceManager.h" // For DeviceState

static constexpr const char* kTag = "ButtonBinder";

void ButtonBinder::StartBinding(const std::vector<DeviceState>& connectedDevices) {
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
            LOG_INFO(kTag, "Captured baseline for joystick '%s' (ID: %u).", SDL_GetJoystickName(dev.joystick), id);
        }
    }
    m_isBinding = true;
    LOG_INFO(kTag, "Started binding process for all connected joysticks.");
}

std::optional<BoundButtonInfo> ButtonBinder::Update(const std::vector<DeviceState>& connectedDevices) {
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
            LOG_WARN(kTag, "Joystick '%s' (ID: %u) button count changed or baseline mismatch. Re-snapshotting.", SDL_GetJoystickName(dev.joystick), id);
            // Re-snapshot this specific joystick's baseline
            joystickBaseline.assign(numButtons, false);
            for (int i = 0; i < numButtons; ++i) joystickBaseline[i] = SDL_GetJoystickButton(dev.joystick, i);
            continue; // Skip this joystick for this update cycle, wait for next.
        }

        for (int i = 0; i < numButtons; ++i) {
            bool currentState = SDL_GetJoystickButton(dev.joystick, i);
            bool wasActiveAtStart = joystickBaseline[i];

            if (currentState != wasActiveAtStart) {
                m_isBinding = false;
                LOG_INFO(kTag, "Button %d change detected on joystick '%s' (ID: %u).", i, SDL_GetJoystickName(dev.joystick), id);
                return BoundButtonInfo{id, i};
            }
        }

        // Elite-style controllers expose paddle buttons through the gamepad
        // abstraction layer only — they are not visible as raw joystick buttons.
        // Scan them explicitly so they can be bound in the mapper.
        // We encode gamepad-only buttons as a negative sentinel:
        //   stored index = -(SDL_GamepadButton + 1)
        // The read path in InputMapper::Update decodes this back to the
        // SDL_GamepadButton and uses SDL_GetGamepadButton for those entries.
        if (dev.gamepad) {
            static const SDL_GamepadButton kPaddleButtons[] = {
                SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
                SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
                SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
                SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
            };
            for (SDL_GamepadButton btn : kPaddleButtons) {
                if (!SDL_GamepadHasButton(dev.gamepad, btn)) continue;
                if (SDL_GetGamepadButton(dev.gamepad, btn)) {
                    int sentinelIndex = -(static_cast<int>(btn) + 1);
                    m_isBinding = false;
                    LOG_INFO(kTag, "Paddle button %d (sentinel %d) detected on gamepad '%s' (ID: %u).",
                             static_cast<int>(btn), sentinelIndex,
                             SDL_GetGamepadName(dev.gamepad), id);
                    return BoundButtonInfo{id, sentinelIndex};
                }
            }
        }
    }
    return std::nullopt;
}

void ButtonBinder::Cancel() {
    if (m_isBinding) LOG_INFO(kTag, "Binding process cancelled.");
    m_isBinding = false;
    m_baselineButtonStates.clear();
}

bool ButtonBinder::IsBindingActive() const {
    return m_isBinding;
}