#pragma once

#include <lo/lo.h>

class OutputMapper;

/**
 * @file HapticDispatcher.h
 * @brief Centralised OSC haptic argument parsing and OutputMapper dispatch.
 *
 * Argument formats
 * --------------------------------------------------------
 * Rumble    iiffi   id, slot, low_freq, high_freq, duration_ms
 * Constant  iifi    id, slot, strength, duration_ms
 * Periodic  iiififfii  id, slot, wave_type, strength, period, magnitude,
 *                      offset, phase, duration_ms
 *           iififfii   (legacy - no wave_type, defaults to Sine)
 * Condition iiiffffffi id, slot, ctype, right_sat, left_sat, right_coeff,
 *                      left_coeff, deadband, center, duration_ms
 * Gain      ii      id, gain
 *
 * DualSense adaptive trigger effects arrive on one fixed OSC path per
 * side x effect (see DispatchDualSense* below), each carrying only the
 * arguments that effect actually uses:
 *   feedback   iii       id, position, strength
 *   weapon     iiii      id, start_position, end_position, strength
 *   vibration  iiii      id, position, amplitude, frequency
 *   multi_position_feedback  iiiiiiiiiii  id, strength_0..strength_9
 *   multi_position_vibration iiiiiiiiiiii id, frequency, amplitude_0..amplitude_9
 *   slope_feedback iiiii id, start_position, end_position, start_strength, end_strength
 *   bow        iiiii     id, start_position, end_position, strength, snap_force
 *   galloping  iiiiii    id, start_position, end_position, first_foot, second_foot, frequency
 *   machine    iiiiiii   id, start_position, end_position, amplitude_a, amplitude_b, frequency, period
 *   off        i         id
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
     *   iiififfii  - new (id, slot, wave_type, strength, period,
     *                     magnitude, offset, phase, duration_ms)
     *   iififfii   - legacy (no wave_type, defaults to Sine)
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

    /**
     * @brief Parse a DualSense adaptive trigger "feedback" message and queue it.
     *
     * Accepted format:  iii  (id, position, strength).  trigger must be
     * "left" or "right" - passed in by the caller since the OSC path
     * (.../left/feedback or .../right/feedback) determines the side.
     */
    static void DispatchDualSenseFeedback(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "weapon" message and queue it.
     *
     * Accepted format:  iiii  (id, start_position, end_position, strength)
     */
    static void DispatchDualSenseWeapon(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "vibration" message and queue it.
     *
     * Accepted format:  iiii  (id, position, amplitude, frequency)
     */
    static void DispatchDualSenseVibration(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "multi_position_feedback" message and queue it.
     *
     * Accepted format:  iiiiiiiiiii  (id, strength_0..strength_9)
     */
    static void DispatchDualSenseMultiPositionFeedback(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "multi_position_vibration" message and queue it.
     *
     * Accepted format:  iiiiiiiiiiii  (id, frequency, amplitude_0..amplitude_9)
     */
    static void DispatchDualSenseMultiPositionVibration(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "slope_feedback" message and queue it.
     *
     * Accepted format:  iiiii  (id, start_position, end_position, start_strength, end_strength)
     */
    static void DispatchDualSenseSlopeFeedback(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "bow" message and queue it.
     *
     * Accepted format:  iiiii  (id, start_position, end_position, strength, snap_force)
     */
    static void DispatchDualSenseBow(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "galloping" message and queue it.
     *
     * Accepted format:  iiiiii  (id, start_position, end_position, first_foot,
     *                            second_foot, frequency)
     */
    static void DispatchDualSenseGalloping(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "machine" message and queue it.
     *
     * Accepted format:  iiiiiii  (id, start_position, end_position, amplitude_a,
     *                             amplitude_b, frequency, period)
     */
    static void DispatchDualSenseMachine(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);

    /**
     * @brief Parse a DualSense adaptive trigger "off" message and queue it.
     *
     * Accepted format:  i  (id)
     */
    static void DispatchDualSenseOff(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger);
};