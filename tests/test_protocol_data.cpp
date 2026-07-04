#include <gtest/gtest.h>
#include "Protocols/ProtocolDefinition.h"
#include "Devices/VirtualDeviceManager.h"

// ═════════════════════════════════════════════════════════════════════════════
// ProtocolDefinition - default values
// ═════════════════════════════════════════════════════════════════════════════

TEST(ProtocolDefinition, DefaultTransportIsOSC) {
    ProtocolDefinition def;
    EXPECT_EQ(def.transport, ProtocolTransport::OSC);
}

TEST(ProtocolDefinition, DefaultDirectionIsOutput) {
    ProtocolDefinition def;
    EXPECT_EQ(def.direction, ProtocolDirection::Output);
}

TEST(ProtocolDefinition, DefaultOSCHost) {
    ProtocolDefinition def;
    EXPECT_EQ(def.oscHost, "127.0.0.1");
}

TEST(ProtocolDefinition, DefaultOSCSendPort) {
    ProtocolDefinition def;
    EXPECT_EQ(def.oscSendPort, 9066);
}

TEST(ProtocolDefinition, DefaultOSCRecvPort) {
    ProtocolDefinition def;
    EXPECT_EQ(def.oscRecvPort, 9068);
}

TEST(ProtocolDefinition, DefaultWSSPort) {
    ProtocolDefinition def;
    EXPECT_EQ(def.wssPort, 4269);
}

TEST(ProtocolDefinition, DefaultActiveIsFalse) {
    ProtocolDefinition def;
    EXPECT_FALSE(def.active);
}

TEST(ProtocolDefinition, DefaultFieldsIsEmpty) {
    ProtocolDefinition def;
    EXPECT_TRUE(def.fields.empty());
}

TEST(ProtocolDefinition, DefaultIdAndNameAreEmpty) {
    ProtocolDefinition def;
    EXPECT_TRUE(def.id.empty());
    EXPECT_TRUE(def.name.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ProtocolDefinition - mutation
// ═════════════════════════════════════════════════════════════════════════════

TEST(ProtocolDefinition, CanSetTransportToWebSocket) {
    ProtocolDefinition def;
    def.transport = ProtocolTransport::WebSocket;
    EXPECT_EQ(def.transport, ProtocolTransport::WebSocket);
}

TEST(ProtocolDefinition, CanSetDirectionToInput) {
    ProtocolDefinition def;
    def.direction = ProtocolDirection::Input;
    EXPECT_EQ(def.direction, ProtocolDirection::Input);
}

TEST(ProtocolDefinition, CanAddField) {
    ProtocolDefinition def;
    ProtocolField f;
    f.fieldId = "axis_steering";
    f.oscPath = "/input/steering";
    f.wsKey   = "steering";
    def.fields.push_back(f);
    ASSERT_EQ(def.fields.size(), 1u);
    EXPECT_EQ(def.fields[0].fieldId, "axis_steering");
}

TEST(ProtocolDefinition, CanActivate) {
    ProtocolDefinition def;
    def.active = true;
    EXPECT_TRUE(def.active);
}

// ═════════════════════════════════════════════════════════════════════════════
// ProtocolField - defaults
// ═════════════════════════════════════════════════════════════════════════════

TEST(ProtocolField, DefaultEnabledIsTrue) {
    ProtocolField f;
    EXPECT_TRUE(f.enabled);
}

TEST(ProtocolField, DefaultStringsAreEmpty) {
    ProtocolField f;
    EXPECT_TRUE(f.fieldId.empty());
    EXPECT_TRUE(f.oscPath.empty());
    EXPECT_TRUE(f.wsKey.empty());
}

TEST(ProtocolField, CanDisable) {
    ProtocolField f;
    f.enabled = false;
    EXPECT_FALSE(f.enabled);
}

// ═════════════════════════════════════════════════════════════════════════════
// FieldDescriptor - defaults and mutation
// ═════════════════════════════════════════════════════════════════════════════

TEST(FieldDescriptor, DefaultIsBuiltInIsFalse) {
    FieldDescriptor fd;
    EXPECT_FALSE(fd.isBuiltIn);
}

TEST(FieldDescriptor, DefaultStringsAreEmpty) {
    FieldDescriptor fd;
    EXPECT_TRUE(fd.id.empty());
    EXPECT_TRUE(fd.label.empty());
    EXPECT_TRUE(fd.category.empty());
    EXPECT_TRUE(fd.defaultOscPath.empty());
    EXPECT_TRUE(fd.defaultWsKey.empty());
}

TEST(FieldDescriptor, CanSetAllFields) {
    FieldDescriptor fd;
    fd.id             = "axis_steering";
    fd.label          = "Steering / Yaw";
    fd.category       = "Analog";
    fd.type           = FieldType::AnalogAxis;
    fd.defaultOscPath = "/input/steering";
    fd.defaultWsKey   = "steering";
    fd.isBuiltIn      = true;

    EXPECT_EQ(fd.id,             "axis_steering");
    EXPECT_EQ(fd.label,          "Steering / Yaw");
    EXPECT_EQ(fd.category,       "Analog");
    EXPECT_EQ(fd.type,           FieldType::AnalogAxis);
    EXPECT_EQ(fd.defaultOscPath, "/input/steering");
    EXPECT_EQ(fd.defaultWsKey,   "steering");
    EXPECT_TRUE(fd.isBuiltIn);
}

TEST(FieldDescriptor, DigitalButtonType) {
    FieldDescriptor fd;
    fd.type = FieldType::DigitalButton;
    EXPECT_EQ(fd.type, FieldType::DigitalButton);
}

// ═════════════════════════════════════════════════════════════════════════════
// VirtualDeviceTypeName helper
// ═════════════════════════════════════════════════════════════════════════════

TEST(VirtualDeviceTypeName, GamepadLabel) {
    EXPECT_STREQ(VirtualDeviceTypeName(VirtualDeviceType::Gamepad), "Gamepad");
}

TEST(VirtualDeviceTypeName, SteeringWheelLabel) {
    EXPECT_STREQ(VirtualDeviceTypeName(VirtualDeviceType::SteeringWheel), "Steering Wheel");
}

TEST(VirtualDeviceTypeName, FlightStickLabel) {
    EXPECT_STREQ(VirtualDeviceTypeName(VirtualDeviceType::FlightStick), "Flight Stick");
}

TEST(VirtualDeviceTypeName, GenericLabel) {
    EXPECT_STREQ(VirtualDeviceTypeName(VirtualDeviceType::Generic), "Generic");
}

// ═════════════════════════════════════════════════════════════════════════════
// ProtocolDefinition - copy semantics (field vector is deep-copied)
// ═════════════════════════════════════════════════════════════════════════════

TEST(ProtocolDefinition, CopyHasIndependentFieldVector) {
    ProtocolDefinition def;
    ProtocolField f;
    f.fieldId = "axis_throttle";
    def.fields.push_back(f);

    ProtocolDefinition copy = def;
    copy.fields[0].fieldId = "axis_brake";

    EXPECT_EQ(def.fields[0].fieldId,  "axis_throttle");
    EXPECT_EQ(copy.fields[0].fieldId, "axis_brake");
}

TEST(ProtocolDefinition, MoveTransfersFields) {
    ProtocolDefinition def;
    ProtocolField f;
    f.fieldId = "axis_clutch";
    def.fields.push_back(f);

    ProtocolDefinition moved = std::move(def);
    ASSERT_EQ(moved.fields.size(), 1u);
    EXPECT_EQ(moved.fields[0].fieldId, "axis_clutch");
}
