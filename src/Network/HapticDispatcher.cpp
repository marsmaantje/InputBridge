#include "HapticDispatcher.h"
#include "Haptics/HapticDevice.h"
#include "../Mappers/OutputMapper.h"
#include <cstdint>

void HapticDispatcher::DispatchRumble(lo_arg** argv, int argc, OutputMapper* mapper)
{
    if (!mapper || argc < 5) return;
    for (int i = 0; i < 5; ++i) { if (!argv[i]) return; }
    const int   id       = argv[0]->i;
    const int   slot     = argv[1]->i;
    const float low      = argv[2]->f;
    const float high     = argv[3]->f;
    const int   duration = argv[4]->i;
    mapper->QueueRumble(id, slot, low, high, duration);
}

void HapticDispatcher::DispatchConstant(lo_arg** argv, int argc, OutputMapper* mapper)
{
    if (!mapper || argc < 4) return;
    for (int i = 0; i < 4; ++i) { if (!argv[i]) return; }
    const int   id       = argv[0]->i;
    const int   slot     = argv[1]->i;
    const float strength = argv[2]->f;
    const int   duration = argv[3]->i;
    mapper->QueueConstantForce(id, slot, strength, duration);
}

void HapticDispatcher::DispatchPeriodic(lo_arg** argv, int argc, OutputMapper* mapper)
{
    if (!mapper) return;
    if (argc >= 9) {
        // New format: i i i f i f f i i
        //   (id, slot, wave_type, strength, period, magnitude, offset, phase, duration)
        const int   id        = argv[0]->i;
        const int   slot      = argv[1]->i;
        const HapticPeriodicType wave_type = PeriodicTypeFromIndex(argv[2]->i);
        const float strength  = argv[3]->f;
        const int   period    = argv[4]->i;
        const float magnitude = argv[5]->f;
        const float offset    = argv[6]->f;
        const int   phase     = argv[7]->i;
        const int   duration  = argv[8]->i;
        mapper->QueuePeriodic(id, slot, wave_type, strength, period, magnitude, offset, phase, duration);
    } else if (argc >= 8) {
        // Legacy format: i i f i f f i i  (no wave_type - defaults to Sine)
        const int   id        = argv[0]->i;
        const int   slot      = argv[1]->i;
        const float strength  = argv[2]->f;
        const int   period    = argv[3]->i;
        const float magnitude = argv[4]->f;
        const float offset    = argv[5]->f;
        const int   phase     = argv[6]->i;
        const int   duration  = argv[7]->i;
        mapper->QueuePeriodic(id, slot, HapticPeriodicType::Sine, strength, period, magnitude, offset, phase, duration);
    }
}

void HapticDispatcher::DispatchCondition(lo_arg** argv, int argc, OutputMapper* mapper)
{
    if (!mapper || argc < 10) return;

    // Guard every slot: a sparse or malformed OSC message can leave argv[i]
    // null even when argc is large enough, causing a segfault on dereference.
    for (int i = 0; i < 10; ++i) {
        if (!argv[i]) return;
    }

    const int   id       = argv[0]->i;
    const int   slot     = argv[1]->i;
    const HapticConditionType ctype = ConditionTypeFromIndex(argv[2]->i);
    const float rsat     = argv[3]->f;
    const float lsat     = argv[4]->f;
    const float rcoeff   = argv[5]->f;
    const float lcoeff   = argv[6]->f;
    const float db       = argv[7]->f;
    const float center   = argv[8]->f;
    const int   duration = argv[9]->i;
    mapper->QueueCondition(id, slot, ctype, rsat, lsat, rcoeff, lcoeff, db, center, duration);
}

void HapticDispatcher::DispatchGain(lo_arg** argv, int argc, OutputMapper* mapper)
{
    if (!mapper || argc < 2) return;
    for (int i = 0; i < 2; ++i) { if (!argv[i]) return; }
    const int id   = argv[0]->i;
    const int gain = argv[1]->i;
    mapper->QueueSetGain(id, gain);
}

void HapticDispatcher::DispatchDualSenseFeedback(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 3) return;
    for (int i = 0; i < 3; ++i) { if (!argv[i]) return; }
    const int id       = argv[0]->i;
    const int position = argv[1]->i;
    const int strength = argv[2]->i;
    mapper->QueueDualSenseTrigger(id, trigger, "feedback",
                                   position, strength, /*end_position*/0,
                                   /*amplitude*/0, /*frequency*/0, /*snap_force*/0,
                                   /*first_foot*/0, /*second_foot*/0, /*period*/0,
                                   /*amplitude_a*/0, /*amplitude_b*/0);
}

void HapticDispatcher::DispatchDualSenseWeapon(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 4) return;
    for (int i = 0; i < 4; ++i) { if (!argv[i]) return; }
    const int id             = argv[0]->i;
    const int start_position = argv[1]->i;
    const int end_position   = argv[2]->i;
    const int strength       = argv[3]->i;
    // "position" doubles as "start_position" downstream (see OutputMapper::TriggerDualSenseTrigger).
    mapper->QueueDualSenseTrigger(id, trigger, "weapon",
                                   start_position, strength, end_position,
                                   /*amplitude*/0, /*frequency*/0, /*snap_force*/0,
                                   /*first_foot*/0, /*second_foot*/0, /*period*/0,
                                   /*amplitude_a*/0, /*amplitude_b*/0);
}

void HapticDispatcher::DispatchDualSenseVibration(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 4) return;
    for (int i = 0; i < 4; ++i) { if (!argv[i]) return; }
    const int id        = argv[0]->i;
    const int position  = argv[1]->i;
    const int amplitude = argv[2]->i;
    const int frequency = argv[3]->i;
    mapper->QueueDualSenseTrigger(id, trigger, "vibration",
                                   position, /*strength*/0, /*end_position*/0,
                                   amplitude, frequency, /*snap_force*/0,
                                   /*first_foot*/0, /*second_foot*/0, /*period*/0,
                                   /*amplitude_a*/0, /*amplitude_b*/0);
}

void HapticDispatcher::DispatchDualSenseMultiPositionFeedback(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 11) return;
    for (int i = 0; i < 11; ++i) { if (!argv[i]) return; }
    const int id = argv[0]->i;
    uint8_t strengths[10];
    for (int i = 0; i < 10; ++i) {
        int v = argv[i + 1]->i;
        strengths[i] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    mapper->QueueDualSenseMultiPositionFeedback(id, trigger, strengths);
}

void HapticDispatcher::DispatchDualSenseMultiPositionVibration(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 12) return;
    for (int i = 0; i < 12; ++i) { if (!argv[i]) return; }
    const int id = argv[0]->i;
    int freq = argv[1]->i;
    uint8_t frequency = static_cast<uint8_t>(freq < 0 ? 0 : (freq > 255 ? 255 : freq));
    uint8_t amplitudes[10];
    for (int i = 0; i < 10; ++i) {
        int v = argv[i + 2]->i;
        amplitudes[i] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    mapper->QueueDualSenseMultiPositionVibration(id, trigger, frequency, amplitudes);
}

void HapticDispatcher::DispatchDualSenseSlopeFeedback(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 5) return;
    for (int i = 0; i < 5; ++i) { if (!argv[i]) return; }
    const int id             = argv[0]->i;
    const int start_position = argv[1]->i;
    const int end_position   = argv[2]->i;
    const int start_strength = argv[3]->i;
    const int end_strength   = argv[4]->i;
    // "strength"/"amplitude" double as "start_strength"/"end_strength" downstream
    // (see OutputMapper::TriggerDualSenseTrigger).
    mapper->QueueDualSenseTrigger(id, trigger, "slope_feedback",
                                   start_position, start_strength, end_position,
                                   end_strength, /*frequency*/0, /*snap_force*/0,
                                   /*first_foot*/0, /*second_foot*/0, /*period*/0,
                                   /*amplitude_a*/0, /*amplitude_b*/0);
}

void HapticDispatcher::DispatchDualSenseBow(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 5) return;
    for (int i = 0; i < 5; ++i) { if (!argv[i]) return; }
    const int id             = argv[0]->i;
    const int start_position = argv[1]->i;
    const int end_position   = argv[2]->i;
    const int strength       = argv[3]->i;
    const int snap_force     = argv[4]->i;
    mapper->QueueDualSenseTrigger(id, trigger, "bow",
                                   start_position, strength, end_position,
                                   /*amplitude*/0, /*frequency*/0, snap_force,
                                   /*first_foot*/0, /*second_foot*/0, /*period*/0,
                                   /*amplitude_a*/0, /*amplitude_b*/0);
}

void HapticDispatcher::DispatchDualSenseGalloping(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 6) return;
    for (int i = 0; i < 6; ++i) { if (!argv[i]) return; }
    const int id             = argv[0]->i;
    const int start_position = argv[1]->i;
    const int end_position   = argv[2]->i;
    const int first_foot     = argv[3]->i;
    const int second_foot    = argv[4]->i;
    const int frequency      = argv[5]->i;
    mapper->QueueDualSenseTrigger(id, trigger, "galloping",
                                   start_position, /*strength*/0, end_position,
                                   /*amplitude*/0, frequency, /*snap_force*/0,
                                   first_foot, second_foot, /*period*/0,
                                   /*amplitude_a*/0, /*amplitude_b*/0);
}

void HapticDispatcher::DispatchDualSenseMachine(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 7) return;
    for (int i = 0; i < 7; ++i) { if (!argv[i]) return; }
    const int id             = argv[0]->i;
    const int start_position = argv[1]->i;
    const int end_position   = argv[2]->i;
    const int amplitude_a    = argv[3]->i;
    const int amplitude_b    = argv[4]->i;
    const int frequency      = argv[5]->i;
    const int period         = argv[6]->i;
    mapper->QueueDualSenseTrigger(id, trigger, "machine",
                                   start_position, /*strength*/0, end_position,
                                   /*amplitude*/0, frequency, /*snap_force*/0,
                                   /*first_foot*/0, /*second_foot*/0, period,
                                   amplitude_a, amplitude_b);
}

void HapticDispatcher::DispatchDualSenseOff(lo_arg** argv, int argc, OutputMapper* mapper, const char* trigger)
{
    if (!mapper || argc < 1) return;
    if (!argv[0]) return;
    const int id = argv[0]->i;
    mapper->QueueDualSenseTrigger(id, trigger, "off",
                                   /*position*/0, /*strength*/0, /*end_position*/0,
                                   /*amplitude*/0, /*frequency*/0, /*snap_force*/0,
                                   /*first_foot*/0, /*second_foot*/0, /*period*/0,
                                   /*amplitude_a*/0, /*amplitude_b*/0);
}