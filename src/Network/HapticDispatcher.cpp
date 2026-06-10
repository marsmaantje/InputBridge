#include "HapticDispatcher.h"
#include "../Mappers/OutputMapper.h"

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
        // Legacy format: i i f i f f i i  (no wave_type — defaults to Sine)
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
