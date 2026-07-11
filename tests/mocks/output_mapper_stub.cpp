// ─────────────────────────────────────────────────────────────────────────────
// output_mapper_stub.cpp
//
// Link-seam replacement for OutputMapper.cpp in the HapticParser test binary.
// Every method is a no-op stub EXCEPT the four Queue* methods called by
// HapticParser::Parse(), which record their arguments into the HapticStub
// vectors declared in output_mapper_stub.h.
//
// This file must be compiled *instead of* (not alongside) OutputMapper.cpp.
// ─────────────────────────────────────────────────────────────────────────────

#include "Mappers/OutputMapper.h"
#include "mocks/output_mapper_stub.h"

#include <memory>
#include <vector>

// ── Global recording state ────────────────────────────────────────────────────

namespace HapticStub {

std::vector<RumbleArgs>    rumbleCalls;
std::vector<ConstantArgs>  constantCalls;
std::vector<PeriodicArgs>  periodicCalls;
std::vector<ConditionArgs> conditionCalls;
std::vector<DualSenseArgs> dualSenseCalls;
std::vector<DualSenseArrayArgs> dualSenseArrayCalls;

void Reset() {
    rumbleCalls.clear();
    constantCalls.clear();
    periodicCalls.clear();
    conditionCalls.clear();
    dualSenseCalls.clear();
    dualSenseArrayCalls.clear();
}

} // namespace HapticStub

// ── Required static member ────────────────────────────────────────────────────

std::unique_ptr<OutputMapper> OutputMapper::s_Instance;

// ── Queue methods: record calls instead of dispatching to SDL ─────────────────
// These are the only methods actually invoked by HapticParser::Parse().
// They do NOT access 'this' - see the stub header for why this is safe.

void OutputMapper::QueueRumble(int id, int slot, float low, float high, int dur) {
    HapticStub::rumbleCalls.push_back({id, slot, low, high, dur});
}

void OutputMapper::QueueConstantForce(int id, int slot, float strength, int dur) {
    HapticStub::constantCalls.push_back({id, slot, strength, dur});
}

void OutputMapper::QueuePeriodic(int id, int slot, HapticPeriodicType wave_type, float strength, int period,
                                 float magnitude, float offset, int phase, int dur) {
    HapticStub::periodicCalls.push_back({id, slot, wave_type, strength, period, magnitude, offset, phase, dur});
}

void OutputMapper::QueueCondition(int id, int slot, HapticConditionType type,
                                  float rsat, float lsat,
                                  float rcoeff, float lcoeff,
                                  float db, float center, int dur) {
    HapticStub::conditionCalls.push_back({id, slot, type, rsat, lsat, rcoeff, lcoeff, db, center, dur});
}

// ── Remaining stubs (never called in HapticParser tests) ─────────────────────

OutputMapper& OutputMapper::GetInstance()                              { return *s_Instance; }
void          OutputMapper::Init(const DeviceManager& dm)             { s_Instance.reset(new OutputMapper(dm)); }
void          OutputMapper::Shutdown()                                 { s_Instance.reset(); }

// The constructor stores a reference to DeviceManager; constructing an
// OutputMapper is not needed in these tests (we use a raw-buffer pointer),
// but the symbol must be defined to satisfy the linker.
OutputMapper::OutputMapper(const DeviceManager& dm) : m_DeviceManager(dm) {}
OutputMapper::~OutputMapper() {}

void OutputMapper::DrawContent()                                       {}
void OutputMapper::DrawContentOnly()                                   {}
void OutputMapper::Update()                                            {}
void OutputMapper::SetActiveHapticTargets(std::vector<HapticTarget>*) {}
void OutputMapper::HandleDeviceConnectionChange()                      {}
void OutputMapper::StopAllHapticEffects()                              {}
bool OutputMapper::IsHapticsActive() const                             { return false; }

void OutputMapper::QueueSetGain(int, int)                              {}
void OutputMapper::QueueDualSenseTrigger(int device, const char* trigger, const char* type,
                                         int p1, int p2, int p3, int p4, int p5, int p6,
                                         int p7, int p8, int p9, int p10, int p11) {
    HapticStub::dualSenseCalls.push_back({
        device, trigger ? trigger : "", type ? type : "",
        p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11
    });
}

void OutputMapper::QueueDualSenseMultiPositionFeedback(int device, const char* trigger, const uint8_t strengths[10]) {
    HapticStub::DualSenseArrayArgs args;
    args.device      = device;
    args.trigger     = trigger ? trigger : "";
    args.effect_type = "multi_position_feedback";
    args.frequency   = 0;
    for (int i = 0; i < 10; ++i) args.values[i] = strengths[i];
    HapticStub::dualSenseArrayCalls.push_back(args);
}

void OutputMapper::QueueDualSenseMultiPositionVibration(int device, const char* trigger, uint8_t frequency, const uint8_t amplitudes[10]) {
    HapticStub::DualSenseArrayArgs args;
    args.device      = device;
    args.trigger     = trigger ? trigger : "";
    args.effect_type = "multi_position_vibration";
    args.frequency   = frequency;
    for (int i = 0; i < 10; ++i) args.values[i] = amplitudes[i];
    HapticStub::dualSenseArrayCalls.push_back(args);
}

// Private helpers - never called from outside; stubs prevent link errors.
void OutputMapper::QueueCommand(HapticCommand&&)                       {}
void OutputMapper::QueueArrayCommand(DualSenseArrayCommand&&)          {}
void OutputMapper::GetTargets(int, std::vector<HapticTarget*>&)        {}
void OutputMapper::UpdateHapticDevice(HapticTarget&)                   {}
void OutputMapper::CloseHapticDevice(HapticTarget&)                    {}
void OutputMapper::TriggerRumble(int, int, float, float, int)          {}
void OutputMapper::TriggerConstantForce(int, int, float, int)          {}
void OutputMapper::TriggerPeriodic(int, int, HapticPeriodicType, float, int, float, float, int, int) {}
void OutputMapper::TriggerCondition(int, int, HapticConditionType,
                                    float, float, float, float, float, float, int) {}
void OutputMapper::TriggerSetGain(int, int)                            {}
void OutputMapper::TriggerDualSenseTrigger(int, const char*, const char*,
                                            int, int, int, int, int, int,
                                            int, int, int, int, int)  {}
void OutputMapper::TriggerDualSenseMultiPositionFeedback(int, const char*, const uint8_t*)              {}
void OutputMapper::TriggerDualSenseMultiPositionVibration(int, const char*, uint8_t, const uint8_t*)    {}