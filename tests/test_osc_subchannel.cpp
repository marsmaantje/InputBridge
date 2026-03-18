#include <gtest/gtest.h>
#include "Network/OSCSubchannel.h"

// ═════════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════════

using E = SubchannelPath::Effect;

// ═════════════════════════════════════════════════════════════════════════════
// Invalid / rejected paths
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, EmptyPath) {
    auto r = ParseSubchannelPath("");
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.effect, E::Unknown);
    EXPECT_EQ(r.slot, -1);
}

TEST(ParseSubchannelPath, RootSlashOnly) {
    auto r = ParseSubchannelPath("/");
    EXPECT_FALSE(r.valid);
}

TEST(ParseSubchannelPath, WrongPrefix) {
    EXPECT_FALSE(ParseSubchannelPath("/osc/rumble/0").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptics/rumble/0").valid);
    EXPECT_FALSE(ParseSubchannelPath("haptic/rumble/0").valid);
}

TEST(ParseSubchannelPath, ExactBasePaths_NoSlot) {
    // Bare effect paths without a trailing slot number must be rejected.
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/constant").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/periodic").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/condition").valid);
}

TEST(ParseSubchannelPath, TrailingSlashNoSlot) {
    // "/haptic/rumble/" — tail is empty, must be rejected.
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/").valid);
}

TEST(ParseSubchannelPath, NonNumericTail) {
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/a").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/1a").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/slot").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/-1").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/1.5").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/ 1").valid);
}

TEST(ParseSubchannelPath, UnknownEffect) {
    // Known prefix + digit suffix, but the effect name is not recognised.
    EXPECT_FALSE(ParseSubchannelPath("/haptic/gain/0").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/spring/0").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/0").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/unknown/3").valid);
}

TEST(ParseSubchannelPath, TooShortPath) {
    EXPECT_FALSE(ParseSubchannelPath("/haptic/").valid);
}

// ═════════════════════════════════════════════════════════════════════════════
// Rumble subchannels
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, Rumble_Slot0) {
    auto r = ParseSubchannelPath("/haptic/rumble/0");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Rumble);
    EXPECT_EQ(r.slot, 0);
}

TEST(ParseSubchannelPath, Rumble_Slot1) {
    auto r = ParseSubchannelPath("/haptic/rumble/1");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Rumble);
    EXPECT_EQ(r.slot, 1);
}

TEST(ParseSubchannelPath, Rumble_SlotMultiDigit) {
    auto r = ParseSubchannelPath("/haptic/rumble/12");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Rumble);
    EXPECT_EQ(r.slot, 12);
}

TEST(ParseSubchannelPath, Rumble_SlotLarge) {
    auto r = ParseSubchannelPath("/haptic/rumble/255");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Rumble);
    EXPECT_EQ(r.slot, 255);
}

// ═════════════════════════════════════════════════════════════════════════════
// Constant force subchannels
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, Constant_Slot0) {
    auto r = ParseSubchannelPath("/haptic/constant/0");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Constant);
    EXPECT_EQ(r.slot, 0);
}

TEST(ParseSubchannelPath, Constant_Slot3) {
    auto r = ParseSubchannelPath("/haptic/constant/3");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Constant);
    EXPECT_EQ(r.slot, 3);
}

TEST(ParseSubchannelPath, Constant_SlotTwoDigit) {
    auto r = ParseSubchannelPath("/haptic/constant/10");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.slot, 10);
}

// ═════════════════════════════════════════════════════════════════════════════
// Periodic subchannels
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, Periodic_Slot0) {
    auto r = ParseSubchannelPath("/haptic/periodic/0");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Periodic);
    EXPECT_EQ(r.slot, 0);
}

TEST(ParseSubchannelPath, Periodic_Slot5) {
    auto r = ParseSubchannelPath("/haptic/periodic/5");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Periodic);
    EXPECT_EQ(r.slot, 5);
}

TEST(ParseSubchannelPath, Periodic_SlotTwoDigit) {
    auto r = ParseSubchannelPath("/haptic/periodic/42");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.slot, 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// Condition subchannels
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, Condition_Slot0) {
    auto r = ParseSubchannelPath("/haptic/condition/0");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Condition);
    EXPECT_EQ(r.slot, 0);
}

TEST(ParseSubchannelPath, Condition_Slot2) {
    auto r = ParseSubchannelPath("/haptic/condition/2");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.effect, E::Condition);
    EXPECT_EQ(r.slot, 2);
}

TEST(ParseSubchannelPath, Condition_SlotTwoDigit) {
    auto r = ParseSubchannelPath("/haptic/condition/99");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.slot, 99);
}

// ═════════════════════════════════════════════════════════════════════════════
// All four effects use distinct enum values
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, EffectEnumsAreDistinct) {
    EXPECT_NE(ParseSubchannelPath("/haptic/rumble/0").effect,
              ParseSubchannelPath("/haptic/constant/0").effect);
    EXPECT_NE(ParseSubchannelPath("/haptic/constant/0").effect,
              ParseSubchannelPath("/haptic/periodic/0").effect);
    EXPECT_NE(ParseSubchannelPath("/haptic/periodic/0").effect,
              ParseSubchannelPath("/haptic/condition/0").effect);
}

// ═════════════════════════════════════════════════════════════════════════════
// Slot 0 is a valid slot (not the same as "absent")
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, SlotZeroIsValid) {
    for (const char* path : {
        "/haptic/rumble/0",
        "/haptic/constant/0",
        "/haptic/periodic/0",
        "/haptic/condition/0",
    }) {
        auto r = ParseSubchannelPath(path);
        EXPECT_TRUE(r.valid) << "path: " << path;
        EXPECT_EQ(r.slot, 0) << "path: " << path;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Resonite multi-slot scenario: same effect, different slots → different paths
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, MultipleRumbleSlotsHaveDistinctPaths) {
    // Two simultaneous rumbles to slots 0 and 1 must have distinct OSC paths.
    auto r0 = ParseSubchannelPath("/haptic/rumble/0");
    auto r1 = ParseSubchannelPath("/haptic/rumble/1");

    EXPECT_TRUE(r0.valid);
    EXPECT_TRUE(r1.valid);
    EXPECT_EQ(r0.effect, r1.effect);  // same effect family
    EXPECT_NE(r0.slot,   r1.slot);    // different slots → different paths
    EXPECT_EQ(r0.slot, 0);
    EXPECT_EQ(r1.slot, 1);
}

TEST(ParseSubchannelPath, MultiplePeriodicsInSameFrame) {
    // Verifies that periodic slots 0-3 all parse correctly — simulating four
    // distinct sine waves sent in the same frame from Resonite.
    for (int i = 0; i < 4; ++i) {
        std::string path = "/haptic/periodic/" + std::to_string(i);
        auto r = ParseSubchannelPath(path);
        EXPECT_TRUE(r.valid)   << "slot " << i;
        EXPECT_EQ(r.effect, E::Periodic) << "slot " << i;
        EXPECT_EQ(r.slot, i)  << "slot " << i;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Gain has no subchannel variant — explicitly rejected
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, GainHasNoSubchannel) {
    // /haptic/gain is a device-wide setting with no slot dimension.
    // Any path of the form /haptic/gain/<N> must be rejected.
    EXPECT_FALSE(ParseSubchannelPath("/haptic/gain/0").valid);
    EXPECT_FALSE(ParseSubchannelPath("/haptic/gain/1").valid);
}

// ═════════════════════════════════════════════════════════════════════════════
// Paths that look similar but are not subchannels
// ═════════════════════════════════════════════════════════════════════════════

TEST(ParseSubchannelPath, LooksLikeSubchannelButIsnt) {
    // Extra path components beyond /haptic/<effect>/<slot>/<extra> are invalid.
    EXPECT_FALSE(ParseSubchannelPath("/haptic/rumble/0/extra").valid);

    // Leading zeros are still digits — these ARE valid integers.
    auto r = ParseSubchannelPath("/haptic/rumble/007");
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.slot, 7);  // leading zeros are fine; value is still 7
}
