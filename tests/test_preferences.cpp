#include <gtest/gtest.h>
#include "Preferences/Preferences.h"

// PreferencesManager::Load() / Save() call SDL_GetBasePath() which needs SDL
// initialized.  All in-memory accessors (Set*/Get*, IsPreferenceApplied, etc.)
// work entirely on std::map state and need no SDL calls, so we test those
// directly — which is the real value anyway: correct key/section routing,
// type coercion, defaults, and deletion.

// ═════════════════════════════════════════════════════════════════════════════
// String accessors
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, GetStringReturnsDefaultWhenKeyAbsent) {
    PreferencesManager prefs;
    EXPECT_EQ(prefs.GetString("missing_key", "default"), "default");
}

TEST(PreferencesManager, SetAndGetStringRoundtrip) {
    PreferencesManager prefs;
    prefs.SetString("theme", "dark");
    EXPECT_EQ(prefs.GetString("theme"), "dark");
}

TEST(PreferencesManager, SetStringOverwritesPreviousValue) {
    PreferencesManager prefs;
    prefs.SetString("theme", "light");
    prefs.SetString("theme", "dark");
    EXPECT_EQ(prefs.GetString("theme"), "dark");
}

TEST(PreferencesManager, SetAndGetStringWithSection) {
    PreferencesManager prefs;
    prefs.SetString("Network", "host", "192.168.1.1");
    EXPECT_EQ(prefs.GetString("Network", "host", "127.0.0.1"), "192.168.1.1");
}

TEST(PreferencesManager, GetStringWithSectionReturnsDefaultWhenAbsent) {
    PreferencesManager prefs;
    EXPECT_EQ(prefs.GetString("Network", "host", "127.0.0.1"), "127.0.0.1");
}

TEST(PreferencesManager, SectionKeysAreIsolatedFromGlobalKeys) {
    PreferencesManager prefs;
    prefs.SetString("key", "global_value");
    prefs.SetString("Section", "key", "section_value");
    EXPECT_EQ(prefs.GetString("key"),           "global_value");
    EXPECT_EQ(prefs.GetString("Section", "key"), "section_value");
}

// ═════════════════════════════════════════════════════════════════════════════
// Int accessors
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, GetIntReturnsDefaultWhenAbsent) {
    PreferencesManager prefs;
    EXPECT_EQ(prefs.GetInt("port", 9066), 9066);
}

TEST(PreferencesManager, SetAndGetIntRoundtrip) {
    PreferencesManager prefs;
    prefs.SetInt("port", 1234);
    EXPECT_EQ(prefs.GetInt("port"), 1234);
}

TEST(PreferencesManager, SetIntNegativeValue) {
    PreferencesManager prefs;
    prefs.SetInt("offset", -42);
    EXPECT_EQ(prefs.GetInt("offset"), -42);
}

TEST(PreferencesManager, SetAndGetIntWithSection) {
    PreferencesManager prefs;
    prefs.SetInt("OSC", "sendPort", 9000);
    EXPECT_EQ(prefs.GetInt("OSC", "sendPort", 0), 9000);
}

TEST(PreferencesManager, GetIntWithSectionReturnsDefaultWhenAbsent) {
    PreferencesManager prefs;
    EXPECT_EQ(prefs.GetInt("OSC", "sendPort", 9066), 9066);
}

// ═════════════════════════════════════════════════════════════════════════════
// Float accessors
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, GetFloatReturnsDefaultWhenAbsent) {
    PreferencesManager prefs;
    EXPECT_FLOAT_EQ(prefs.GetFloat("gain", 1.0f), 1.0f);
}

TEST(PreferencesManager, SetAndGetFloatRoundtrip) {
    PreferencesManager prefs;
    prefs.SetFloat("gain", 0.75f);
    EXPECT_FLOAT_EQ(prefs.GetFloat("gain"), 0.75f);
}

TEST(PreferencesManager, SetAndGetFloatWithSection) {
    PreferencesManager prefs;
    prefs.SetFloat("Haptics", "strength", 0.5f);
    EXPECT_FLOAT_EQ(prefs.GetFloat("Haptics", "strength", 1.0f), 0.5f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Bool accessors
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, GetBoolReturnsDefaultWhenAbsent) {
    PreferencesManager prefs;
    EXPECT_FALSE(prefs.GetBool("enabled", false));
    EXPECT_TRUE(prefs.GetBool("enabled", true));
}

TEST(PreferencesManager, SetAndGetBoolTrueRoundtrip) {
    PreferencesManager prefs;
    prefs.SetBool("autostart", true);
    EXPECT_TRUE(prefs.GetBool("autostart"));
}

TEST(PreferencesManager, SetAndGetBoolFalseRoundtrip) {
    PreferencesManager prefs;
    prefs.SetBool("autostart", false);
    EXPECT_FALSE(prefs.GetBool("autostart"));
}

TEST(PreferencesManager, SetAndGetBoolWithSection) {
    PreferencesManager prefs;
    prefs.SetBool("OSC", "enabled", true);
    EXPECT_TRUE(prefs.GetBool("OSC", "enabled", false));
}

// ═════════════════════════════════════════════════════════════════════════════
// DeleteKey
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, DeleteKeyRemovesGlobalKey) {
    PreferencesManager prefs;
    prefs.SetString("theme", "dark");
    prefs.DeleteKey("theme");
    EXPECT_EQ(prefs.GetString("theme", "default"), "default");
}

TEST(PreferencesManager, DeleteKeyWithSectionRemovesSectionKey) {
    PreferencesManager prefs;
    prefs.SetString("OSC", "host", "1.2.3.4");
    prefs.DeleteKey("OSC", "host");
    EXPECT_EQ(prefs.GetString("OSC", "host", "127.0.0.1"), "127.0.0.1");
}

TEST(PreferencesManager, DeleteKeyDoesNotCrashForAbsentKey) {
    PreferencesManager prefs;
    EXPECT_NO_THROW(prefs.DeleteKey("nonexistent_key"));
}

TEST(PreferencesManager, DeleteKeyOnlySectionKeyDoesNotAffectGlobalKey) {
    PreferencesManager prefs;
    prefs.SetString("key", "global");
    prefs.SetString("Section", "key", "section");
    prefs.DeleteKey("Section", "key");
    EXPECT_EQ(prefs.GetString("key"), "global");
}

// ═════════════════════════════════════════════════════════════════════════════
// Visualizer preferences
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, SetAndGetVisualizerPreference) {
    PreferencesManager prefs;
    prefs.SetVisualizerPreference("GUID-ABCDEF", "SteeringWheel");
    EXPECT_EQ(prefs.GetVisualizerPreference("GUID-ABCDEF"), "SteeringWheel");
}

TEST(PreferencesManager, GetVisualizerPreferenceReturnsEmptyWhenAbsent) {
    PreferencesManager prefs;
    EXPECT_TRUE(prefs.GetVisualizerPreference("GUID-MISSING").empty());
}

TEST(PreferencesManager, VisualizerPreferenceOverwrite) {
    PreferencesManager prefs;
    prefs.SetVisualizerPreference("GUID-XYZ", "Gamepad");
    prefs.SetVisualizerPreference("GUID-XYZ", "Generic");
    EXPECT_EQ(prefs.GetVisualizerPreference("GUID-XYZ"), "Generic");
}

TEST(PreferencesManager, MultipleVisualizerPreferencesAreIndependent) {
    PreferencesManager prefs;
    prefs.SetVisualizerPreference("GUID-1", "Gamepad");
    prefs.SetVisualizerPreference("GUID-2", "SteeringWheel");
    EXPECT_EQ(prefs.GetVisualizerPreference("GUID-1"), "Gamepad");
    EXPECT_EQ(prefs.GetVisualizerPreference("GUID-2"), "SteeringWheel");
}

// ═════════════════════════════════════════════════════════════════════════════
// Device mapping
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, SetAndGetDeviceMapping) {
    PreferencesManager prefs;
    prefs.SetDeviceMapping("GUID-WHEEL", "steering_wheel_map");
    EXPECT_EQ(prefs.GetDeviceMapping("GUID-WHEEL"), "steering_wheel_map");
}

TEST(PreferencesManager, GetDeviceMappingReturnsEmptyWhenAbsent) {
    PreferencesManager prefs;
    EXPECT_TRUE(prefs.GetDeviceMapping("GUID-NONE").empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// Applied preferences tracking (SDL_JoystickID is just an int type)
// ═════════════════════════════════════════════════════════════════════════════

TEST(PreferencesManager, IsPreferenceAppliedFalseInitially) {
    PreferencesManager prefs;
    EXPECT_FALSE(prefs.IsPreferenceApplied(42));
}

TEST(PreferencesManager, MarkPreferenceAppliedMakesItTrue) {
    PreferencesManager prefs;
    prefs.MarkPreferenceApplied(42);
    EXPECT_TRUE(prefs.IsPreferenceApplied(42));
}

TEST(PreferencesManager, ClearAppliedPreferenceMakesItFalse) {
    PreferencesManager prefs;
    prefs.MarkPreferenceApplied(42);
    prefs.ClearAppliedPreference(42);
    EXPECT_FALSE(prefs.IsPreferenceApplied(42));
}

TEST(PreferencesManager, AppliedPreferencesAreIndependent) {
    PreferencesManager prefs;
    prefs.MarkPreferenceApplied(1);
    prefs.MarkPreferenceApplied(2);
    prefs.ClearAppliedPreference(1);
    EXPECT_FALSE(prefs.IsPreferenceApplied(1));
    EXPECT_TRUE(prefs.IsPreferenceApplied(2));
}

TEST(PreferencesManager, MarkAlreadyAppliedDoesNotCrash) {
    PreferencesManager prefs;
    prefs.MarkPreferenceApplied(99);
    EXPECT_NO_THROW(prefs.MarkPreferenceApplied(99));
    EXPECT_TRUE(prefs.IsPreferenceApplied(99));
}
