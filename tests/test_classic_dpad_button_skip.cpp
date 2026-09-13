// test_classic_dpad_button_skip.cpp
//
// Unit tests for InputBridge::Wiimote::ShouldSkipButtonInButtonsSection() and
// its helper IsClassicDPadButtonIndex(), declared in
// Devices/Wiimote/WiimoteVirtualBridge.h.
//
// Background: the Wiimote virtual bridge exposes the Classic Controller's
// D-Pad twice in its raw layout - once as Hat_ClassicDPad (a real hat) and
// once as four separate digital buttons (Btn_ClassicUp/Down/Left/Right),
// the latter kept so each direction stays individually bindable in
// InputMapper/MappingProfileStore. GenericVisualizer's "Buttons" section
// used to draw all four of those buttons as well as the hat, showing the
// D-Pad twice; ShouldSkipButtonInButtonsSection() is the single predicate
// both GenericVisualizer.cpp and this test use to keep that skip correct.
//
// These tests exercise the predicate directly - no ImGui, no SDL joystick
// I/O, no DeviceState construction beyond a bare device-name string.

#include <gtest/gtest.h>
#include "Devices/Wiimote/WiimoteVirtualBridge.h"

using namespace InputBridge::Wiimote;

// ═════════════════════════════════════════════════════════════════════════════
// IsClassicDPadButtonIndex
// ═════════════════════════════════════════════════════════════════════════════

TEST(IsClassicDPadButtonIndex, TrueForAllFourClassicDPadDirections) {
    EXPECT_TRUE(IsClassicDPadButtonIndex(Btn_ClassicUp));
    EXPECT_TRUE(IsClassicDPadButtonIndex(Btn_ClassicDown));
    EXPECT_TRUE(IsClassicDPadButtonIndex(Btn_ClassicLeft));
    EXPECT_TRUE(IsClassicDPadButtonIndex(Btn_ClassicRight));
}

TEST(IsClassicDPadButtonIndex, FalseForClassicFaceAndShoulderButtons) {
    // Only the D-Pad directions should match - every other Classic Controller
    // button (face buttons, shoulders, triggers, system buttons) must still
    // be drawn normally.
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicA));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicB));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicX));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicY));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicL));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicR));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicZL));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicZR));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicPlus));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicMinus));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_ClassicHome));
}

TEST(IsClassicDPadButtonIndex, FalseForNonClassicWiimoteButtons) {
    // The plain Wiimote's own buttons (including its own D-Pad, which is a
    // hat with no Btn_* equivalent at all) must be unaffected.
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_A));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_B));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_One));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_Two));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_Plus));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_Minus));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_Home));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_NunchukC));
    EXPECT_FALSE(IsClassicDPadButtonIndex(Btn_NunchukZ));
}

TEST(IsClassicDPadButtonIndex, FalseForOutOfRangeIndices) {
    EXPECT_FALSE(IsClassicDPadButtonIndex(-1));
    EXPECT_FALSE(IsClassicDPadButtonIndex(kWiimoteNumButtons));
    EXPECT_FALSE(IsClassicDPadButtonIndex(1000));
}

// ═════════════════════════════════════════════════════════════════════════════
// ShouldSkipButtonInButtonsSection
// ═════════════════════════════════════════════════════════════════════════════

TEST(ShouldSkipButtonInButtonsSection, SkipsClassicDPadOnWiimoteBridge) {
    const std::string dev = kWiimoteBridgeDeviceName;
    EXPECT_TRUE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicUp));
    EXPECT_TRUE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicDown));
    EXPECT_TRUE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicLeft));
    EXPECT_TRUE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicRight));
}

TEST(ShouldSkipButtonInButtonsSection, DoesNotSkipOtherButtonsOnWiimoteBridge) {
    const std::string dev = kWiimoteBridgeDeviceName;
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_A));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_Home));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicA));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicPlus));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicHome));
}

TEST(ShouldSkipButtonInButtonsSection, NeverSkipsOnBalanceBoardBridge) {
    // The Balance Board bridge has no D-Pad at all (nhats=0, and its only
    // button is BBtn_A) but shares no button indices with the Wiimote
    // bridge's enum in a way that could accidentally collide - confirm the
    // device-name check alone is sufficient to keep it fully unaffected,
    // even if asked about indices that happen to numerically match a
    // Classic D-Pad direction on the other bridge.
    const std::string dev = kBalanceBoardBridgeDeviceName;
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, BBtn_A));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicUp));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicDown));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicLeft));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicRight));
}

TEST(ShouldSkipButtonInButtonsSection, NeverSkipsOnUnrelatedDevices) {
    // A regular SDL gamepad (or literally any device name other than the
    // two bridge constants) must never have any of its buttons skipped,
    // even if the raw index happens to equal one of the Btn_Classic* values.
    const std::string dev = "Xbox Wireless Controller";
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicUp));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicDown));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicLeft));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, Btn_ClassicRight));
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(dev, 0));
}

TEST(ShouldSkipButtonInButtonsSection, EmptyDeviceNameNeverSkips) {
    EXPECT_FALSE(ShouldSkipButtonInButtonsSection(std::string(), Btn_ClassicUp));
}
