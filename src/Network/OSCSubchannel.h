#pragma once

#include <string_view>

/**
 * @file OSCSubchannel.h
 *
 * Subchannel OSC path support for haptic effects.
 *
 * Problem
 * -------
 * Hosts such as Resonite only deliver *one* OSC message per path per frame.
 * The existing haptic paths (/haptic/rumble, /haptic/constant, …) encode the
 * destination slot as a message *argument*, which means a sender can only
 * address one slot per effect type per frame.
 *
 * Solution
 * --------
 * Subchannel paths encode the slot in the path itself:
 *
 *   /haptic/rumble/<slot>
 *   /haptic/constant/<slot>
 *   /haptic/periodic/<slot>
 *   /haptic/condition/<slot>
 *
 * Because each slot gets a distinct OSC path, a host that limits messages to
 * one per path per frame can still address multiple slots simultaneously.
 *
 * Message formats (slot absent from arguments — it is in the path)
 * ----------------------------------------------------------------
 *   /haptic/rumble/<N>       iffi   id, low_freq, high_freq, duration_ms
 *   /haptic/constant/<N>     ifi    id, strength, duration_ms
 *   /haptic/periodic/<N>     iififfii  id, wave_type, strength, period,
 *                                      magnitude, offset, phase, duration_ms
 *                                      wave_type: 0=Sine 1=Square 2=Triangle
 *                                                 3=SawtoothUp 4=SawtoothDown
 *   /haptic/periodic/<N>     ififfii   (legacy — no wave_type, defaults Sine)
 *   /haptic/condition/<N>    iiffffffi id, condition_type, right_sat, left_sat,
 *                                      right_coeff, left_coeff, deadband,
 *                                      center, duration_ms
 *
 * /haptic/gain has no slot dimension and therefore has no subchannel variant.
 *
 * ParseSubchannelPath()
 * ---------------------
 * The parsing logic is extracted here so it can be tested independently of the
 * liblo runtime.  OSCServer::haptic_subchannel_handler calls this function and
 * then dispatches based on the returned Effect enum.
 */

/// Parsed representation of a subchannel OSC path.
struct SubchannelPath {
    enum class Effect {
        Unknown,    ///< Path did not match any known effect.
        Rumble,
        Constant,
        Periodic,
        Condition,
    };

    /// Which haptic effect family the path addresses.
    Effect effect = Effect::Unknown;

    /// The slot number extracted from the trailing path component.
    /// Valid only when @c valid is true; otherwise -1.
    int slot = -1;

    /// True iff the path is a well-formed subchannel path for a known effect.
    bool valid = false;
};

/**
 * @brief Parse an OSC path and determine whether it is a subchannel path.
 *
 * A valid subchannel path has the form:
 *   /haptic/<effect>/<N>
 * where <effect> is one of {rumble, constant, periodic, condition} and <N> is
 * a non-negative decimal integer with no leading sign or whitespace.
 *
 * Returns a SubchannelPath with @c valid == false for any path that does not
 * match this pattern exactly (wrong prefix, unknown effect name, non-integer
 * tail, etc.).  The function never throws.
 */
SubchannelPath ParseSubchannelPath(std::string_view path) noexcept;
