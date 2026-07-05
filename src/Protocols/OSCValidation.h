#pragma once
// OscValidation.h - lightweight helpers used by OSCProtocol and OSCBaseProtocol
// to guard against malformed or out-of-range haptic messages before they reach
// SDL's haptic driver (which can crash or assert on invalid values).
//
// Design goals:
//   • Null-safety: reject messages with null path/types/argv pointers.
//   • Type-tag + argc matching: already done at the call site via strcmp/argc
//     checks, so we only need value-range guards here.
//   • Clamping over rejection for float ranges: a slightly-out-of-range
//     magnitude is better delivered clamped than silently dropped.
//   • Hard rejection for int enums: an unknown condition/wave type index cannot
//     be safely defaulted, so we reject and log.

#include "lo/lo_osc_types.h"
#include "App/Log.h"
#include <string_view>
#include <algorithm>

namespace OscValidation {

static constexpr const char* kTag = "OSCValidation";

// ── Pointer safety ────────────────────────────────────────────────────────────

inline bool CheckPointers(const char* path, const char* types, lo_arg** argv, int argc) {
    if (!path) {
        LOG_WARN(kTag, "Received OSC message with null path - ignored");
        return false;
    }
    if (!types) {
        LOG_WARN(kTag, "Received OSC message on '%s' with null type string - ignored", path);
        return false;
    }
    if (argc > 0 && !argv) {
        LOG_WARN(kTag, "Received OSC message on '%s' with argc=%d but null argv - ignored", path, argc);
        return false;
    }
    for (int i = 0; i < argc; ++i) {
        if (!argv[i]) {
            LOG_WARN(kTag, "Received OSC message on '%s': argv[%d] is null - ignored", path, i);
            return false;
        }
    }
    return true;
}

// ── Slot ─────────────────────────────────────────────────────────────────────

static constexpr int kMaxSlot = 255;

inline bool ValidateSlot(int slot, std::string_view path) {
    if (slot < 0 || slot > kMaxSlot) {
        LOG_WARN(kTag, "OSC '%.*s': slot %d is out of range [0, %d] - ignored",
            (int)path.size(), path.data(), slot, kMaxSlot);
        return false;
    }
    return true;
}

// ── Float clamping ────────────────────────────────────────────────────────────

inline float Clamp(float v, float lo, float hi, const char* paramName, std::string_view path) {
    if (v < lo || v > hi) {
        float clamped = std::clamp(v, lo, hi);
        LOG_WARN(kTag, "OSC '%.*s': %s value %.4f out of range [%.1f, %.1f] - clamped to %.4f",
            (int)path.size(), path.data(), paramName, v, lo, hi, clamped);
        return clamped;
    }
    return v;
}

inline float ClampNorm(float v, const char* n, std::string_view p)     { return Clamp(v,  0.0f,  1.0f, n, p); }
inline float ClampSigned(float v, const char* n, std::string_view p)   { return Clamp(v, -1.0f,  1.0f, n, p); }
inline float ClampStrength(float v, const char* n, std::string_view p) { return Clamp(v, -1.0f,  1.0f, n, p); }

// ── Int enum guards ───────────────────────────────────────────────────────────

inline bool ValidateWaveType(int idx, std::string_view path) {
    if (idx < 0 || idx > 4) {
        LOG_WARN(kTag, "OSC '%.*s': wave_type %d is out of range [0, 4] - ignored",
            (int)path.size(), path.data(), idx);
        return false;
    }
    return true;
}

inline bool ValidateConditionType(int idx, std::string_view path) {
    if (idx < 0 || idx > 3) {
        LOG_WARN(kTag, "OSC '%.*s': condition_type %d is out of range [0, 3] - ignored",
            (int)path.size(), path.data(), idx);
        return false;
    }
    return true;
}

// ── DualSense-specific int clamps ─────────────────────────────────────────────

inline int ClampInt(int v, int lo, int hi, const char* paramName, std::string_view path) {
    if (v < lo || v > hi) {
        int clamped = std::clamp(v, lo, hi);
        LOG_WARN(kTag, "OSC '%.*s': %s value %d out of range [%d, %d] - clamped to %d",
            (int)path.size(), path.data(), paramName, v, lo, hi, clamped);
        return clamped;
    }
    return v;
}

inline int ClampGain(int v, std::string_view p)        { return ClampInt(v,   0, 100, "gain",           p); }
inline int ClampDSPosition(int v, std::string_view p)  { return ClampInt(v,   0,   9, "position",       p); }
inline int ClampDSStrength(int v, std::string_view p)  { return ClampInt(v,   0,   8, "strength",       p); }
inline int ClampDSAmplitude(int v, std::string_view p) { return ClampInt(v,   0,   8, "amplitude",      p); }
inline int ClampDSFrequency(int v, std::string_view p) { return ClampInt(v,   0, 255, "frequency",      p); }

// Weapon start/end position matches ApplyTriggerEffect's getParam("start_position",2,2,7) and getParam("end_position",7,0,8).
inline int ClampDSStartPos(int v, std::string_view p)  { return ClampInt(v,   2,   7, "start_position", p); }
inline int ClampDSEndPos(int v, std::string_view p)    { return ClampInt(v,   0,   8, "end_position",   p); }

// Bow start/end position and snap force - 0-8, matches ApplyTriggerEffect's "bow" branch.
inline int ClampDSBowPos(int v, std::string_view p)       { return ClampInt(v, 0, 8, "position",   p); }
inline int ClampDSSnapForce(int v, std::string_view p)    { return ClampInt(v, 0, 8, "snap_force", p); }

// Galloping start/end position (0-9) and foot indices matches ApplyTriggerEffect's "galloping" branch.
inline int ClampDSGallopingPos(int v, std::string_view p) { return ClampInt(v, 0, 9, "position",     p); }
inline int ClampDSFirstFoot(int v, std::string_view p)    { return ClampInt(v, 0, 6, "first_foot",   p); }
inline int ClampDSSecondFoot(int v, std::string_view p)   { return ClampInt(v, 0, 7, "second_foot",  p); }

// Machine start/end position (0-9), amplitude A/B (0-7), and period (0-2) matches ApplyTriggerEffect's "machine" branch.
inline int ClampDSMachinePos(int v, std::string_view p)   { return ClampInt(v, 0, 9, "position",     p); }
inline int ClampDSAmplitudeAB(int v, std::string_view p)  { return ClampInt(v, 0, 7, "amplitude",    p); }
inline int ClampDSPeriod(int v, std::string_view p)       { return ClampInt(v, 0, 2, "period",       p); }

} // namespace OscValidation