#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// "Listen for the next input and bind it" state machine, used by the mapping
// UI's Bind buttons. Pulled out of InputMapper's StartListening/
// UpdateListening/CancelListening.
//
// The original UpdateListening() was a single ~350-line function that
// resolved one listening session against six different kinds of hardware
// events (a button-binder hit, a battery level/charging change, a cap-sense
// touch change, a raw axis change, an IMU/touch sensor change, a hat change),
// with the "write the result into digitalMappings[i] or buttonMappings[i]"
// branch duplicated almost verbatim in four of those six places. Update()
// below keeps the same priority order but delegates each kind of event to
// its own small Try* method, and those four duplicated branches now share
// one ApplyDigitalBinding() helper.
// ─────────────────────────────────────────────────────────────────────────────

#include "Devices/SensorState.h"
#include "Mappers/ButtonBinder.h"
#include "MappingTypes.h"
#include <SDL3/SDL.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

class DeviceManager;
struct DeviceState;

namespace InputMapping {

class InputBindingListener {
  public:
    enum class Type { None, Axis, Digital };

    // Begins a listening session: `type` selects whether we're waiting for
    // an axis-ish input (regular axis, IMU sensor, touch, battery level) or
    // a digital-ish input (button, hat, cap-sense touch, battery charging).
    // `name` is the field id being bound for Axis sessions, or "digital" /
    // "button_analog" (selecting which mapping list `index` refers to) for
    // Digital sessions.
    void StartListening(Type type, const std::string& name, int index, const DeviceManager& dm);
    void CancelListening();

    bool IsActive() const { return m_State.active; }
    Type ActiveType() const { return m_State.type; }
    bool IsListeningFor(Type type, const std::string& name, int index = -1) const;

    // Polls hardware for a completed binding and writes it into `profile` if
    // one is found. Returns true if a binding was just completed (the caller
    // should persist the profile); the session is auto-cancelled either way
    // once a terminating event is detected. No-op (returns false) if not
    // currently listening.
    bool Update(MappingProfile& profile, const DeviceManager& dm);

  private:
    struct AxisBaseline {
        SDL_JoystickID instance_id;
        int axis_index;
        Sint16 value;
    };
    struct SensorBaseline {
        SDL_JoystickID instance_id = 0;
        GyroState gyro, gyroL, gyroR;
        AccelState accel, accelL, accelR;
        TouchState touch;
        bool capSenseLeftStick = false, capSenseRightStick = false;
        bool capSenseLeftGrip = false, capSenseRightGrip = false;
        int batteryLevel = -1;
        bool charging = false;
    };
    struct State {
        bool active = false;
        Type type = Type::None;
        std::string targetName;
        int listIndex = -1;
        std::vector<AxisBaseline> initialAxes;
        std::map<SDL_JoystickID, std::vector<Uint8>> initialHatStates;
        std::vector<SensorBaseline> initialSensors;
    };
    State m_State;
    ButtonBinder m_ButtonBinder;

    const SensorBaseline* FindBaseline(SDL_JoystickID id) const;
    InputSource* ResolveAxisSource(MappingProfile& profile, const std::string& name) const;

    // Writes a completed digital binding into digitalMappings[listIndex] or
    // buttonMappings[listIndex] depending on m_State.targetName. Returns
    // true if the index was valid and the write happened.
    bool ApplyDigitalBinding(MappingProfile& profile, const std::string& device_guid,
                              SDL_JoystickID instance_id, int button_index, int hat_index,
                              int hat_mask, InputSource::SensorChannel sensor_channel);

    // Each Try* below returns nullopt if its kind of event wasn't detected
    // this call (caller moves on to the next check), or the "should save"
    // result if it was (the session has already been cancelled by the time
    // it returns).
    std::optional<bool> TryResolveButtonBinderHit(MappingProfile& profile, const DeviceManager& dm);
    std::optional<bool> TryResolveAxisBatteryChange(MappingProfile& profile, const DeviceManager& dm);
    std::optional<bool> TryResolveDigitalChargingChange(MappingProfile& profile, const DeviceManager& dm);
    std::optional<bool> TryResolveCapSenseChange(MappingProfile& profile, const DeviceManager& dm);
    std::optional<bool> TryResolveAxisChange(MappingProfile& profile, const DeviceManager& dm);
    std::optional<bool> TryResolveSensorChange(MappingProfile& profile, const DeviceManager& dm);
    std::optional<bool> TryResolveHatChange(MappingProfile& profile, const DeviceManager& dm);
};

} // namespace InputMapping
