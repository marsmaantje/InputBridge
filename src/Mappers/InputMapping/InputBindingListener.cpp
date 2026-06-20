#include "InputBindingListener.h"

#include "App/Log.h"
#include "Devices/DeviceManager.h"
#include "Devices/SensorReader.h"
#include <cmath>
#include <stdexcept>

static constexpr const char* kTag = "InputMapper";

namespace InputMapping {

namespace {

const DeviceState* FindDeviceState(SDL_JoystickID id, const std::vector<DeviceState>& devices) {
    for (const auto& d : devices)
        if (d.instance_id == id) return &d;
    return nullptr;
}

} // namespace

void InputBindingListener::StartListening(Type type, const std::string& name, int index,
                                           const DeviceManager& dm) {
    m_State.active = true;
    m_State.type = type;
    m_State.targetName = name;
    m_State.listIndex = index;
    m_State.initialHatStates.clear();
    m_State.initialSensors.clear();
    m_State.initialAxes.clear();

    for (const auto& dev : dm.GetDevices()) {
        if (!dev.joystick) continue;

        // Capture initial axis states if listening for Axis
        if (type == Type::Axis) {
            for (int i = 0; i < dev.num_axes; ++i)
                m_State.initialAxes.push_back({dev.instance_id, i, SDL_GetJoystickAxis(dev.joystick, i)});
        }

        // Capture initial sensor/battery states (IMU for gamepads, battery for all)
        SensorBaseline ss;
        ss.instance_id = dev.instance_id;
        if (dev.gamepad) {
            SensorReader::EnableAll(dev.gamepad); // Ensure sensors are enabled for baseline capture
            ss.gyro = SensorReader::ReadGyro(dev.gamepad);
            ss.accel = SensorReader::ReadAccel(dev.gamepad);
            ss.gyroL = SensorReader::ReadGyroL(dev.gamepad);
            ss.accelL = SensorReader::ReadAccelL(dev.gamepad);
            ss.gyroR = SensorReader::ReadGyroR(dev.gamepad);
            ss.accelR = SensorReader::ReadAccelR(dev.gamepad);
            ss.touch = SensorReader::ReadTouch(dev.gamepad);
            ss.capSenseLeftStick = SDL_GetGamepadCapSense(dev.gamepad, SDL_GAMEPAD_CAPSENSE_LEFT_STICK);
            ss.capSenseRightStick = SDL_GetGamepadCapSense(dev.gamepad, SDL_GAMEPAD_CAPSENSE_RIGHT_STICK);
            ss.capSenseLeftGrip = SDL_GetGamepadCapSense(dev.gamepad, SDL_GAMEPAD_CAPSENSE_LEFT_GRIP);
            ss.capSenseRightGrip = SDL_GetGamepadCapSense(dev.gamepad, SDL_GAMEPAD_CAPSENSE_RIGHT_GRIP);
        }

        int pct = -1;
        SDL_PowerState ps = SDL_POWERSTATE_UNKNOWN;
        if (dev.gamepad) ps = SDL_GetGamepadPowerInfo(dev.gamepad, &pct);
        else if (dev.joystick) ps = SDL_GetJoystickPowerInfo(dev.joystick, &pct);

        ss.batteryLevel = pct;
        ss.charging = (ps == SDL_POWERSTATE_CHARGING || ps == SDL_POWERSTATE_CHARGED);
        m_State.initialSensors.push_back(ss);
    }

    if (type == Type::Digital) {
        m_ButtonBinder.StartBinding(dm.GetDevices());
        for (const auto& dev : dm.GetDevices()) { // Capture hat baselines
            if (!dev.joystick) continue;
            int numHats = SDL_GetNumJoystickHats(dev.joystick);
            if (numHats > 0) {
                std::vector<Uint8> states(numHats);
                for (int i = 0; i < numHats; ++i) states[i] = SDL_GetJoystickHat(dev.joystick, i);
                m_State.initialHatStates[dev.instance_id] = states;
            }
        }
    }
}

void InputBindingListener::CancelListening() {
    m_State.active = false;
    m_State.initialAxes.clear();
    m_State.initialSensors.clear();
    m_State.initialHatStates.clear();
    m_ButtonBinder.Cancel();
}

bool InputBindingListener::IsListeningFor(Type type, const std::string& name, int index) const {
    if (!m_State.active || m_State.type != type || m_State.targetName != name) return false;
    return index == -1 || m_State.listIndex == index;
}

const InputBindingListener::SensorBaseline* InputBindingListener::FindBaseline(SDL_JoystickID id) const {
    for (const auto& s : m_State.initialSensors)
        if (s.instance_id == id) return &s;
    return nullptr;
}

// "__a2d_N"   → analogToDigitalMappings[N].source
// "__mix_M_S" → channelMixes[M].sources[S].source
// anything else → outputToInput[name]
InputSource* InputBindingListener::ResolveAxisSource(MappingProfile& profile, const std::string& name) const {
    static const std::string a2dPfx = "__a2d_";
    static const std::string mixPfx = "__mix_";
    if (name.size() > a2dPfx.size() && name.substr(0, a2dPfx.size()) == a2dPfx) {
        int idx = std::stoi(name.substr(a2dPfx.size()));
        if (idx >= 0 && idx < (int)profile.analogToDigitalMappings.size())
            return &profile.analogToDigitalMappings[idx].source;
        return nullptr;
    }
    if (name.size() > mixPfx.size() && name.substr(0, mixPfx.size()) == mixPfx) {
        std::string rest = name.substr(mixPfx.size());
        auto sep = rest.find('_');
        if (sep != std::string::npos) {
            int mi = std::stoi(rest.substr(0, sep));
            int si = std::stoi(rest.substr(sep + 1));
            if (mi >= 0 && mi < (int)profile.channelMixes.size() &&
                si >= 0 && si < (int)profile.channelMixes[mi].sources.size())
                return &profile.channelMixes[mi].sources[si].source;
        }
        return nullptr;
    }
    return &profile.outputToInput[name];
}

bool InputBindingListener::ApplyDigitalBinding(MappingProfile& profile, const std::string& device_guid,
                                                SDL_JoystickID instance_id, int button_index, int hat_index,
                                                int hat_mask, InputSource::SensorChannel sensor_channel) {
    if (m_State.targetName == "digital") {
        if (m_State.listIndex < 0 || m_State.listIndex >= (int)profile.digitalMappings.size()) return false;
        auto& dm = profile.digitalMappings[m_State.listIndex];
        dm.device_guid = device_guid;
        dm.instance_id = instance_id;
        dm.button_index = button_index;
        dm.hat_index = hat_index;
        dm.hat_mask = hat_mask;
        dm.sensor_channel = sensor_channel;
        return true;
    }
    if (m_State.targetName == "button_analog") {
        if (m_State.listIndex < 0 || m_State.listIndex >= (int)profile.buttonMappings.size()) return false;
        auto& bm = profile.buttonMappings[m_State.listIndex];
        bm.device_guid = device_guid;
        bm.instance_id = instance_id;
        bm.button_index = button_index;
        bm.hat_index = hat_index;
        bm.hat_mask = hat_mask;
        bm.sensor_channel = sensor_channel;
        return true;
    }
    return false;
}

std::optional<bool> InputBindingListener::TryResolveButtonBinderHit(MappingProfile& profile,
                                                                      const DeviceManager& dm) {
    auto bound = m_ButtonBinder.Update(dm.GetDevices());
    if (!bound) return std::nullopt;

    const DeviceState* boundDeviceState = FindDeviceState(bound->joystickID, dm.GetDevices());
    if (!boundDeviceState) {
        LOG_WARN(kTag, "Bound joystick (ID: %u) not found for button binding.", bound->joystickID);
        CancelListening();
        return false;
    }

    bool updated = ApplyDigitalBinding(profile, DeviceManager::GetDeviceGUIDString(*boundDeviceState),
                                        bound->joystickID, bound->buttonIndex, -1, 0,
                                        InputSource::SensorChannel::None);
    CancelListening();
    return updated;
}

std::optional<bool> InputBindingListener::TryResolveAxisBatteryChange(MappingProfile& profile,
                                                                        const DeviceManager& dm) {
    using SC = InputSource::SensorChannel;
    for (const auto& dev : dm.GetDevices()) {
        const auto* baseline = FindBaseline(dev.instance_id);
        if (!baseline) continue;

        int curPercent = -1;
        SDL_PowerState curPower = SDL_POWERSTATE_UNKNOWN;
        if (dev.gamepad) curPower = SDL_GetGamepadPowerInfo(dev.gamepad, &curPercent);
        else if (dev.joystick) curPower = SDL_GetJoystickPowerInfo(dev.joystick, &curPercent);
        bool isCurrentlyCharging = (curPower == SDL_POWERSTATE_CHARGING || curPower == SDL_POWERSTATE_CHARGED);

        SC channel;
        if (curPercent != baseline->batteryLevel && curPercent != -1) channel = SC::BatteryLevel;
        else if (isCurrentlyCharging != baseline->charging) channel = SC::BatteryCharging;
        else continue;

        InputSource& src = *ResolveAxisSource(profile, m_State.targetName);
        src.deviceGuid = DeviceManager::GetDeviceGUIDString(dev);
        src.instance_id = dev.instance_id;
        src.axisIndex = -1;
        src.sensorChannel = channel;
        CancelListening();
        return true;
    }
    return std::nullopt;
}

std::optional<bool> InputBindingListener::TryResolveDigitalChargingChange(MappingProfile& profile,
                                                                            const DeviceManager& dm) {
    for (const auto& dev : dm.GetDevices()) {
        const auto* baseline = FindBaseline(dev.instance_id);
        if (!baseline) continue;

        int curPercent = -1;
        SDL_PowerState curPower = SDL_POWERSTATE_UNKNOWN;
        if (dev.gamepad) curPower = SDL_GetGamepadPowerInfo(dev.gamepad, &curPercent);
        else if (dev.joystick) curPower = SDL_GetJoystickPowerInfo(dev.joystick, &curPercent);
        bool isCurrentlyCharging = (curPower == SDL_POWERSTATE_CHARGING || curPower == SDL_POWERSTATE_CHARGED);

        if (isCurrentlyCharging == baseline->charging) continue;

        bool updated = ApplyDigitalBinding(profile, DeviceManager::GetDeviceGUIDString(dev), dev.instance_id,
                                            -1, -1, 0, InputSource::SensorChannel::BatteryCharging);
        CancelListening();
        return updated;
    }
    return std::nullopt;
}

std::optional<bool> InputBindingListener::TryResolveCapSenseChange(MappingProfile& profile,
                                                                      const DeviceManager& dm) {
    using SC = InputSource::SensorChannel;
    for (const auto& dev : dm.GetDevices()) {
        if (!dev.gamepad) continue;
        const auto* baseline = FindBaseline(dev.instance_id);
        if (!baseline) continue;

        struct Check { SDL_GamepadCapSenseType type; bool baselineVal; SC channel; };
        const Check checks[] = {
            {SDL_GAMEPAD_CAPSENSE_LEFT_STICK, baseline->capSenseLeftStick, SC::LeftStickTouch},
            {SDL_GAMEPAD_CAPSENSE_RIGHT_STICK, baseline->capSenseRightStick, SC::RightStickTouch},
            {SDL_GAMEPAD_CAPSENSE_LEFT_GRIP, baseline->capSenseLeftGrip, SC::LeftGripTouch},
            {SDL_GAMEPAD_CAPSENSE_RIGHT_GRIP, baseline->capSenseRightGrip, SC::RightGripTouch},
        };
        for (const auto& c : checks) {
            if (SDL_GetGamepadCapSense(dev.gamepad, c.type) == c.baselineVal) continue;
            bool updated = ApplyDigitalBinding(profile, DeviceManager::GetDeviceGUIDString(dev), dev.instance_id,
                                                -1, -1, 0, c.channel);
            CancelListening();
            return updated;
        }
    }
    return std::nullopt;
}

std::optional<bool> InputBindingListener::TryResolveAxisChange(MappingProfile& profile,
                                                                  const DeviceManager& dm) {
    for (const auto& dev : dm.GetDevices()) {
        if (!dev.joystick) continue;
        for (int i = 0; i < dev.num_axes; ++i) {
            Sint16 val = SDL_GetJoystickAxis(dev.joystick, i);
            Sint16 baseline = 0;
            bool found = false;
            for (const auto& as : m_State.initialAxes) {
                if (as.instance_id == dev.instance_id && as.axis_index == i) {
                    baseline = as.value;
                    found = true;
                    break;
                }
            }
            if (!found || std::abs((int)val - (int)baseline) <= 10000) continue;

            InputSource& src = *ResolveAxisSource(profile, m_State.targetName);
            src.deviceGuid = DeviceManager::GetDeviceGUIDString(dev);
            src.instance_id = dev.instance_id;
            src.axisIndex = i;
            src.sensorChannel = InputSource::SensorChannel::None;
            CancelListening();
            return true;
        }
    }
    return std::nullopt;
}

std::optional<bool> InputBindingListener::TryResolveSensorChange(MappingProfile& profile,
                                                                    const DeviceManager& dm) {
    using SC = InputSource::SensorChannel;
    for (const auto& dev : dm.GetDevices()) {
        if (!dev.gamepad) continue;
        const auto* baseline = FindBaseline(dev.instance_id);
        if (!baseline) continue;

        auto g = SensorReader::ReadGyro(dev.gamepad);
        auto a = SensorReader::ReadAccel(dev.gamepad);
        auto t = SensorReader::ReadTouch(dev.gamepad);
        auto gl = SensorReader::ReadGyroL(dev.gamepad);
        auto al = SensorReader::ReadAccelL(dev.gamepad);
        auto gr = SensorReader::ReadGyroR(dev.gamepad);
        auto ar = SensorReader::ReadAccelR(dev.gamepad);

        auto checkSensor = [&](float cur, float base, SC ch, float threshold) -> bool {
            if (std::abs(cur - base) <= threshold) return false;
            InputSource& src = *ResolveAxisSource(profile, m_State.targetName);
            src.deviceGuid = DeviceManager::GetDeviceGUIDString(dev);
            src.instance_id = dev.instance_id;
            src.axisIndex = -1;
            src.sensorChannel = ch;
            return true;
        };

        // Cap-sense (stick/grip touch) inputs are boolean — they are intentionally
        // excluded here so the Bind button never resolves them as analog sources.
        // They are handled exclusively in the Digital listening branch (TryResolveCapSenseChange).

        // Use a significant threshold to avoid triggering on sensor noise/jitter.
        const bool hit =
            checkSensor(g.x, baseline->gyro.x, SC::GyroX, 0.4f) ||
            checkSensor(g.y, baseline->gyro.y, SC::GyroY, 0.4f) ||
            checkSensor(g.z, baseline->gyro.z, SC::GyroZ, 0.4f) ||
            checkSensor(a.x, baseline->accel.x, SC::AccelX, 0.4f) ||
            checkSensor(a.y, baseline->accel.y, SC::AccelY, 0.4f) ||
            checkSensor(a.z, baseline->accel.z, SC::AccelZ, 0.4f) ||
            checkSensor(gl.x, baseline->gyroL.x, SC::GyroLX, 0.4f) ||
            checkSensor(gl.y, baseline->gyroL.y, SC::GyroLY, 0.4f) ||
            checkSensor(gl.z, baseline->gyroL.z, SC::GyroLZ, 0.4f) ||
            checkSensor(al.x, baseline->accelL.x, SC::AccelLX, 0.4f) ||
            checkSensor(al.y, baseline->accelL.y, SC::AccelLY, 0.4f) ||
            checkSensor(al.z, baseline->accelL.z, SC::AccelLZ, 0.4f) ||
            checkSensor(gr.x, baseline->gyroR.x, SC::GyroRX, 0.4f) ||
            checkSensor(gr.y, baseline->gyroR.y, SC::GyroRY, 0.4f) ||
            checkSensor(gr.z, baseline->gyroR.z, SC::GyroRZ, 0.4f) ||
            checkSensor(ar.x, baseline->accelR.x, SC::AccelRX, 0.4f) ||
            checkSensor(ar.y, baseline->accelR.y, SC::AccelRY, 0.4f) ||
            checkSensor(ar.z, baseline->accelR.z, SC::AccelRZ, 0.4f);
        if (hit) { CancelListening(); return true; }

        if (t.available) {
            // For touchpads, we look for activation (pressure) or significant movement.
            bool touchHit = false;
            if (t.fingers[0].active) {
                touchHit = checkSensor(t.fingers[0].pressure, baseline->touch.fingers[0].pressure, SC::TouchPressure, 0.3f) ||
                           checkSensor(t.fingers[0].x, baseline->touch.fingers[0].x, SC::TouchX, 0.2f) ||
                           checkSensor(t.fingers[0].y, baseline->touch.fingers[0].y, SC::TouchY, 0.2f);
            }
            if (!touchHit && t.fingers[1].active) {
                touchHit = checkSensor(t.fingers[1].x, baseline->touch.fingers[1].x, SC::Touch2X, 0.2f) ||
                           checkSensor(t.fingers[1].y, baseline->touch.fingers[1].y, SC::Touch2Y, 0.2f) ||
                           checkSensor(t.fingers[1].pressure, baseline->touch.fingers[1].pressure, SC::Touch2Pressure, 0.3f);
            }
            if (touchHit) { CancelListening(); return true; }
        }
    }
    return std::nullopt;
}

std::optional<bool> InputBindingListener::TryResolveHatChange(MappingProfile& profile,
                                                                 const DeviceManager& dm) {
    for (const auto& dev : dm.GetDevices()) {
        if (!dev.joystick) continue;
        auto itHatBaseline = m_State.initialHatStates.find(dev.instance_id);
        const std::vector<Uint8>* hatBaseline =
            itHatBaseline != m_State.initialHatStates.end() ? &itHatBaseline->second : nullptr;

        for (int i = 0; i < dev.num_hats; ++i) {
            Uint8 currentHatState = SDL_GetJoystickHat(dev.joystick, i);
            Uint8 initialHatState =
                hatBaseline && hatBaseline->size() > (size_t)i ? (*hatBaseline)[i] : SDL_HAT_CENTERED;
            if (currentHatState == initialHatState) continue;

            // Bind the active direction (if centered, bind the direction it just left).
            int hatMask = (currentHatState != SDL_HAT_CENTERED) ? currentHatState : initialHatState;
            bool updated = ApplyDigitalBinding(profile, DeviceManager::GetDeviceGUIDString(dev), dev.instance_id,
                                                -1, i, hatMask, InputSource::SensorChannel::None);
            CancelListening();
            return updated;
        }
    }
    return std::nullopt;
}

bool InputBindingListener::Update(MappingProfile& profile, const DeviceManager& dm) {
    if (!m_State.active) return false;

    if (m_ButtonBinder.IsBindingActive()) {
        if (auto r = TryResolveButtonBinderHit(profile, dm)) return *r;
    }

    if (m_State.type == Type::Axis) {
        if (auto r = TryResolveAxisBatteryChange(profile, dm)) return *r;
    } else if (m_State.type == Type::Digital) {
        if (auto r = TryResolveDigitalChargingChange(profile, dm)) return *r;
        if (auto r = TryResolveCapSenseChange(profile, dm)) return *r;
    }

    if (m_State.type == Type::Axis) {
        if (auto r = TryResolveAxisChange(profile, dm)) return *r;
        if (auto r = TryResolveSensorChange(profile, dm)) return *r;
    } else if (m_State.type == Type::Digital) {
        if (auto r = TryResolveHatChange(profile, dm)) return *r;
    }

    return false;
}

} // namespace InputMapping
