#pragma once

#include <lo/lo.h>
#include "Haptics/HapticDevice.h"

class OutputMapper;

/**
 * @file HapticDispatcher.h
 * @brief Centralised OSC haptic argument parsing and OutputMapper dispatch.
 *
 * Problem
 * -------
 * Before this class existed, argument parsing and QueueXxx() calls were
 * duplicated in five independent liblo handler functions
 * (haptic_rumble_handler, haptic_constant_handler, …) **and** again in the
 * subchannel switch-statement inside haptic_subchannel_handler.  Any change
 * to argument layout or queue-call signature had to be made in two places.
 *
 * Solution
 * --------
 * HapticDispatcher owns the single authoritative implementation for each
 * effect type.  Both the per-path handlers and the subchannel handler now
 * delegate here:
 *
 * @code
 *   // Fixed path handler (e.g. /haptic/rumble)
 *   HapticDispatcher::DispatchRumble(argv, argc, mapper);
 *
 *   // Subchannel handler (e.g. /haptic/rumble/3)
 *   HapticDispatcher::DispatchRumble(argv, argc, mapper); // same call
 * @endcode
 *
 * Argument formats (unchanged from the original protocol)
 * --------------------------------------------------------
 * Rumble    iiffi   id, slot, low_freq, high_freq, duration_ms
 * Constant  iifi    id, slot, strength, duration_ms
 * Periodic  iiififfii  id, slot, wave_type, strength, period, magnitude,
 *                      offset, phase, duration_ms
 *           iififfii   (legacy — no wave_type, defaults to Sine)
 * Condition iiiffffffi id, slot, ctype, right_sat, left_sat, right_coeff,
 *                      left_coeff, deadband, center, duration_ms
 * Gain      ii      id, gain
 *
 * All methods are no-ops when mapper is nullptr or argc is insufficient,
 * so callers need no null-guards beyond passing the right mapper pointer.
 */
class HapticDispatcher {
public:
    HapticDispatcher() = delete;

    /**
     * @brief Parse a rumble message and queue it on the OutputMapper.
     *
     * Accepted format:  iiffi  (id, slot, low_freq, high_freq, duration_ms)
     */
    static void DispatchRumble(lo_arg** argv, int argc, OutputMapper* mapper);

    /**
     * @brief Parse a constant-force message and queue it.
     *
     * Accepted format:  iifi  (id, slot, strength, duration_ms)
     */
    static void DispatchConstant(lo_arg** argv, int argc, OutputMapper* mapper);

    /**
     * @brief Parse a periodic message and queue it.
     *
     * Accepted formats:
     *   iiififfii  — new (id, slot, wave_type, strength, period,
     *                     magnitude, offset, phase, duration_ms)
     *   iififfii   — legacy (no wave_type, defaults to Sine)
     */
    static void DispatchPeriodic(lo_arg** argv, int argc, OutputMapper* mapper);

    /**
     * @brief Parse a condition message and queue it.
     *
     * Accepted format:  iiiffffffi  (id, slot, ctype, right_sat, left_sat,
     *                                right_coeff, left_coeff, deadband,
     *                                center, duration_ms)
     */
    static void DispatchCondition(lo_arg** argv, int argc, OutputMapper* mapper);

    /**
     * @brief Parse a gain message and queue it.
     *
     * Accepted format:  ii  (id, gain 0–100)
     */
    static void DispatchGain(lo_arg** argv, int argc, OutputMapper* mapper);
};
