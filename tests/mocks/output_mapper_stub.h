#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Output-mapper link-seam stub
//
// HapticParser::Parse() takes an OutputMapper*, but OutputMapper is a concrete
// class tightly coupled to SDL and DeviceManager.  Rather than initialising all
// of that machinery in unit tests, we use a *link seam*: the test binary
// compiles output_mapper_stub.cpp instead of OutputMapper.cpp.
//
// The stub's Queue* methods ignore 'this' entirely and record every call into
// the global vectors declared below.  The tests use any non-null pointer as the
// "mapper" - conventionally the address of a small static char buffer - because
// the methods never dereference it.
//
// Call HapticStub::Reset() in SetUp() to clear state between tests.
// ─────────────────────────────────────────────────────────────────────────────

#include <vector>
#include "Haptics/HapticDevice.h"

namespace HapticStub {

struct RumbleArgs {
    int     device;
    int     slot;
    float   low;
    float   high;
    int     duration;
};

struct ConstantArgs {
    int   device;
    int   slot;
    float strength;
    int   duration;
};

struct PeriodicArgs {
    int                device;
    int                slot;
    HapticPeriodicType wave_type;
    float              strength;
    int                period;
    float              magnitude;
    float              offset;
    int                phase;
    int                duration;
};

struct ConditionArgs {
    int                device;
    int                slot;
    HapticConditionType type;   // 0=Spring, 1=Damper, 2=Inertia, 3=Friction
    float              right_sat;
    float              left_sat;
    float              right_coeff;
    float              left_coeff;
    float              deadband;
    float              center;
    int                duration;
};

struct DualSenseArgs {
    int         device;
    std::string trigger;
    std::string effect_type;
    int         p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11;
};

// Populated by the stub implementations in output_mapper_stub.cpp.
extern std::vector<RumbleArgs>    rumbleCalls;
extern std::vector<ConstantArgs>  constantCalls;
extern std::vector<PeriodicArgs>  periodicCalls;
extern std::vector<ConditionArgs> conditionCalls;
extern std::vector<DualSenseArgs> dualSenseCalls;

/// Clear all recorded calls.  Call this in test SetUp().
void Reset();

} // namespace HapticStub