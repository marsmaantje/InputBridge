#include "App/Log.h"
#include "VirtualDeviceManager.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Preset tables
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct Preset {
    SDL_JoystickType sdlType;
    int              naxes;
    int              nbuttons;
    VirtualAxisInfo  axisInfo[8]; // generous upper bound
    const char*      buttonLabels[16];
};

static const Preset kGamepadPreset = {
    SDL_JOYSTICK_TYPE_GAMEPAD, 6, 15,
    {
        { "Left X",     0.0f },
        { "Left Y",     0.0f },
        { "Right X",    0.0f },
        { "Right Y",    0.0f },
        { "L Trigger",  -1.0f }, // SDL reports at -32768 when fully released
        { "R Trigger",  -1.0f },
    },
    { "South","East","West","North","Back","Guide","Start",
      "L Stick","R Stick","L Shoulder","R Shoulder",
      "D-Up","D-Down","D-Left","D-Right" }
};

static const Preset kWheelPreset = {
    SDL_JOYSTICK_TYPE_WHEEL, 4, 12,
    {
        { "Steering",  0.0f },
        { "Throttle", -1.0f },
        { "Brake",    -1.0f },
        { "Clutch",   -1.0f },
    },
    { "Btn 0","Btn 1","Btn 2","Btn 3","Btn 4","Btn 5",
      "Btn 6","Btn 7","Btn 8","Btn 9","Btn 10","Btn 11" }
};

static const Preset kFlightPreset = {
    SDL_JOYSTICK_TYPE_FLIGHT_STICK, 6, 12,
    {
        { "Pitch (X)",  0.0f },
        { "Roll (Y)",   0.0f },
        { "Yaw (Z)",    0.0f },
        { "Trim X",     0.0f },
        { "Trim Y",     0.0f },
        { "Throttle",  -1.0f },
    },
    { "Trigger","Btn 1","Btn 2","Btn 3","Btn 4","Btn 5",
      "Btn 6","Btn 7","Btn 8","Btn 9","Btn 10","Btn 11" }
};

static const Preset kGenericPreset = {
    SDL_JOYSTICK_TYPE_UNKNOWN, 4, 8,
    {
        { "Axis 0", 0.0f },
        { "Axis 1", 0.0f },
        { "Axis 2", 0.0f },
        { "Axis 3", 0.0f },
    },
    { "Btn 0","Btn 1","Btn 2","Btn 3","Btn 4","Btn 5","Btn 6","Btn 7" }
};

const Preset& GetPreset(VirtualDeviceType t) {
    switch (t) {
        case VirtualDeviceType::Gamepad:       return kGamepadPreset;
        case VirtualDeviceType::SteeringWheel: return kWheelPreset;
        case VirtualDeviceType::FlightStick:   return kFlightPreset;
        default:                               return kGenericPreset;
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
VirtualDeviceManager& VirtualDeviceManager::GetInstance() {
    static VirtualDeviceManager s_instance;
    return s_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// State factory
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<VirtualDeviceState> VirtualDeviceManager::MakeState(
    VirtualDeviceType type, const std::string& name,
    SDL_JoystickID id, SDL_Joystick* joystick)
{
    const Preset& p = GetPreset(type);

    auto state = std::make_unique<VirtualDeviceState>();
    state->joystick_id = id;
    state->joystick    = joystick;
    state->name        = name;
    state->type        = type;

    state->axes.resize(p.naxes);
    state->axisInfo.resize(p.naxes);
    for (int i = 0; i < p.naxes; ++i) {
        state->axisInfo[i] = p.axisInfo[i];
        state->axes[i]     = p.axisInfo[i].defaultValue;
    }

    state->buttons.assign(p.nbuttons, false);
    state->hat = SDL_HAT_CENTERED;

    return state;
}

// ─────────────────────────────────────────────────────────────────────────────
// AddDevice
// ─────────────────────────────────────────────────────────────────────────────
SDL_JoystickID VirtualDeviceManager::AddDevice(VirtualDeviceType type,
                                                const std::string& name)
{
    const Preset& p = GetPreset(type);

    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type     = static_cast<Uint16>(p.sdlType);
    desc.naxes    = static_cast<Uint16>(p.naxes);
    desc.nbuttons = static_cast<Uint16>(p.nbuttons);
    desc.nhats    = 1;
    desc.name     = name.c_str();

    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        LOG_ERROR("VirtualDeviceManager", "VirtualDeviceManager: SDL_AttachVirtualJoystick failed: %s",
                SDL_GetError());
        return 0;
    }

    SDL_Joystick* joystick = SDL_OpenJoystick(id);
    if (!joystick) {
        LOG_ERROR("VirtualDeviceManager", "VirtualDeviceManager: SDL_OpenJoystick failed: %s", SDL_GetError());
        SDL_DetachVirtualJoystick(id);
        return 0;
    }

    m_Devices.push_back(MakeState(type, name, id, joystick));

    // Push defaults immediately so SDL has a consistent initial state.
    PushState(id);

    LOG_INFO("VirtualDeviceManager", "VirtualDeviceManager: created '%s' (type=%d, id=%u)",
            name.c_str(), static_cast<int>(type), static_cast<unsigned>(id));
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
// RemoveDevice
// ─────────────────────────────────────────────────────────────────────────────
void VirtualDeviceManager::RemoveDevice(SDL_JoystickID id) {
    auto it = std::find_if(m_Devices.begin(), m_Devices.end(),
        [id](const auto& s) { return s->joystick_id == id; });

    if (it == m_Devices.end()) return;

    // Close the open handle first, then detach.
    // SDL_DetachVirtualJoystick fires SDL_EVENT_JOYSTICK_REMOVED which causes
    // DeviceManager::HandleDeviceRemoved() to call SDL_CloseJoystick on the
    // same handle — so we must NOT close it ourselves here.
    SDL_DetachVirtualJoystick(id);

    m_Devices.erase(it);
    LOG_INFO("VirtualDeviceManager", "VirtualDeviceManager: removed virtual device id=%u",
            static_cast<unsigned>(id));
}

// ─────────────────────────────────────────────────────────────────────────────
// PushState  –  called each frame; writes into SDL so SDL_GetJoystickAxis etc.
//              return the values that the UI has set.
// ─────────────────────────────────────────────────────────────────────────────
void VirtualDeviceManager::PushState(SDL_JoystickID id) {
    VirtualDeviceState* s = GetState(id);
    if (!s || !s->joystick) return;

    for (int i = 0; i < static_cast<int>(s->axes.size()); ++i) {
        float  v   = s->axes[i];
        Sint16 raw = static_cast<Sint16>(v * 32767.0f);
        SDL_SetJoystickVirtualAxis(s->joystick, i, raw);
    }

    for (int i = 0; i < static_cast<int>(s->buttons.size()); ++i)
        SDL_SetJoystickVirtualButton(s->joystick, i, s->buttons[i]);

    SDL_SetJoystickVirtualHat(s->joystick, 0, s->hat);
}

void VirtualDeviceManager::PushAllStates() {
    for (const auto& dev : m_Devices)
        PushState(dev->joystick_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────
VirtualDeviceState* VirtualDeviceManager::GetState(SDL_JoystickID id) {
    for (auto& s : m_Devices)
        if (s->joystick_id == id) return s.get();
    return nullptr;
}

const std::vector<std::unique_ptr<VirtualDeviceState>>&
VirtualDeviceManager::GetDevices() const {
    return m_Devices;
}