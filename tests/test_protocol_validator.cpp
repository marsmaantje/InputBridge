#include <gtest/gtest.h>
#include "Core/ProtocolValidator.h"
#include "Protocols/ProtocolDefinition.h"

// ═════════════════════════════════════════════════════════════════════════════
// ValidationResult
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidationResult, StartsValid) {
    ValidationResult r;
    EXPECT_TRUE(r.IsValid());
    EXPECT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.warnings.empty());
}

TEST(ValidationResult, AddErrorMakesInvalid) {
    ValidationResult r;
    r.AddError("something broke");
    EXPECT_FALSE(r.IsValid());
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0], "something broke");
}

TEST(ValidationResult, AddWarningKeepsValid) {
    ValidationResult r;
    r.AddWarning("heads up");
    EXPECT_TRUE(r.IsValid());
    ASSERT_EQ(r.warnings.size(), 1u);
}

TEST(ValidationResult, MultipleErrorsAccumulate) {
    ValidationResult r;
    r.AddError("error one");
    r.AddError("error two");
    EXPECT_EQ(r.errors.size(), 2u);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidationResult, FormattedMessageContainsErrors) {
    ValidationResult r;
    r.AddError("bad port");
    r.AddWarning("missing host");
    std::string msg = r.GetFormattedMessage();
    EXPECT_NE(msg.find("bad port"),     std::string::npos);
    EXPECT_NE(msg.find("missing host"), std::string::npos);
}

TEST(ValidationResult, FormattedMessageEmptyWhenClean) {
    ValidationResult r;
    EXPECT_TRUE(r.GetFormattedMessage().empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateProtocolName
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidateProtocolName, EmptyIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateProtocolName("").empty());
}

TEST(ValidateProtocolName, NormalNameIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateProtocolName("My Racing Protocol").empty());
}

TEST(ValidateProtocolName, Exactly127CharsIsValid) {
    std::string name(127, 'a');
    EXPECT_TRUE(ProtocolValidator::ValidateProtocolName(name).empty());
}

TEST(ValidateProtocolName, Over127CharsIsError) {
    std::string name(128, 'a');
    EXPECT_FALSE(ProtocolValidator::ValidateProtocolName(name).empty());
}

TEST(ValidateProtocolName, ControlCharIsError) {
    std::string name = "Valid\x01Name";
    EXPECT_FALSE(ProtocolValidator::ValidateProtocolName(name).empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateHostAddress
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidateHostAddress, EmptyIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateHostAddress("").empty());
}

TEST(ValidateHostAddress, LoopbackIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateHostAddress("127.0.0.1").empty());
}

TEST(ValidateHostAddress, BroadcastIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateHostAddress("255.255.255.255").empty());
}

TEST(ValidateHostAddress, LocalhostStringIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateHostAddress("localhost").empty());
}

TEST(ValidateHostAddress, HostnameIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateHostAddress("my-server.local").empty());
}

TEST(ValidateHostAddress, InvalidAddressIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateHostAddress("not a host!!").empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidatePort
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidatePort, ZeroIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidatePort(0).empty());
}

TEST(ValidatePort, OneIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidatePort(1).empty());
}

TEST(ValidatePort, TypicalPortIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidatePort(9068).empty());
}

TEST(ValidatePort, MaxPortIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidatePort(65535).empty());
}

TEST(ValidatePort, OverMaxIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidatePort(65536).empty());
}

TEST(ValidatePort, NegativeIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidatePort(-1).empty());
}

TEST(ValidatePort, ErrorMessageContainsPortType) {
    std::string err = ProtocolValidator::ValidatePort(0, "OSC send port");
    EXPECT_NE(err.find("OSC send port"), std::string::npos);
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateOSCPath
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidateOSCPath, EmptyIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateOSCPath("").empty());
}

TEST(ValidateOSCPath, MissingLeadingSlashIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateOSCPath("haptic/rumble").empty());
}

TEST(ValidateOSCPath, RootSlashIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateOSCPath("/").empty());
}

TEST(ValidateOSCPath, TypicalHapticPathIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateOSCPath("/haptic/rumble").empty());
}

TEST(ValidateOSCPath, PathWithUnderscoreAndDotIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateOSCPath("/wheel/steer_axis.0").empty());
}

TEST(ValidateOSCPath, DoubleSlashIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateOSCPath("/haptic//rumble").empty());
}

TEST(ValidateOSCPath, InvalidCharIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateOSCPath("/haptic/rumble!").empty());
}

TEST(ValidateOSCPath, SpaceIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateOSCPath("/haptic/my effect").empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateWSKey
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidateWSKey, EmptyIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateWSKey("").empty());
}

TEST(ValidateWSKey, SimpleKeyIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateWSKey("steering").empty());
}

TEST(ValidateWSKey, KeyWithUnderscoreIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateWSKey("left_trigger").empty());
}

TEST(ValidateWSKey, KeyWithHyphenIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateWSKey("my-key").empty());
}

TEST(ValidateWSKey, StartsWithNumberIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateWSKey("1starts_bad").empty());
}

TEST(ValidateWSKey, Over64CharsIsError) {
    std::string key(65, 'a');
    EXPECT_FALSE(ProtocolValidator::ValidateWSKey(key).empty());
}

TEST(ValidateWSKey, Exactly64CharsIsValid) {
    std::string key(64, 'a');
    EXPECT_TRUE(ProtocolValidator::ValidateWSKey(key).empty());
}

TEST(ValidateWSKey, InvalidCharIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateWSKey("key@invalid").empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateFieldId
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidateFieldId, EmptyIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateFieldId("").empty());
}

TEST(ValidateFieldId, AlphanumericWithUnderscoreIsValid) {
    EXPECT_TRUE(ProtocolValidator::ValidateFieldId("axis_steering").empty());
}

TEST(ValidateFieldId, HyphenIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateFieldId("field-id").empty());
}

TEST(ValidateFieldId, SpaceIsError) {
    EXPECT_FALSE(ProtocolValidator::ValidateFieldId("field id").empty());
}

TEST(ValidateFieldId, Exactly64CharsIsValid) {
    std::string id(64, 'a');
    EXPECT_TRUE(ProtocolValidator::ValidateFieldId(id).empty());
}

TEST(ValidateFieldId, Over64CharsIsError) {
    std::string id(65, 'a');
    EXPECT_FALSE(ProtocolValidator::ValidateFieldId(id).empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateProtocolJSON
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidateProtocolJSON, MissingIdIsError) {
    json j = { {"name","Test"}, {"transport","osc"}, {"direction","send"} };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidateProtocolJSON, MissingNameIsError) {
    json j = { {"id","abc"}, {"transport","osc"}, {"direction","send"} };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidateProtocolJSON, MissingTransportIsError) {
    json j = { {"id","abc"}, {"name","Test"}, {"direction","send"} };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidateProtocolJSON, MissingDirectionIsError) {
    json j = { {"id","abc"}, {"name","Test"}, {"transport","osc"} };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidateProtocolJSON, InvalidTransportIsError) {
    json j = { {"id","abc"}, {"name","Test"}, {"transport","serial"}, {"direction","send"} };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidateProtocolJSON, InvalidDirectionIsError) {
    json j = { {"id","abc"}, {"name","Test"}, {"transport","osc"}, {"direction","both"} };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidateProtocolJSON, LegacyOutputInputDirectionStillValid) {
    // Files exported by older builds use "output"/"input" instead of the
    // current "send"/"receive" - both must keep validating.
    json outJ = { {"id","abc"}, {"name","Test"}, {"transport","osc"}, {"direction","output"} };
    EXPECT_TRUE(ProtocolValidator::ValidateProtocolJSON(outJ).IsValid());

    json inJ = { {"id","abc"}, {"name","Test"}, {"transport","osc"}, {"direction","input"} };
    EXPECT_TRUE(ProtocolValidator::ValidateProtocolJSON(inJ).IsValid());
}

TEST(ValidateProtocolJSON, ValidMinimalOSCIsValid) {
    json j = {
        {"id","abc"}, {"name","Test"}, {"transport","osc"}, {"direction","send"},
        {"oscHost","127.0.0.1"}, {"oscSendPort",9066}, {"fields", json::array()}
    };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_TRUE(r.IsValid());
}

TEST(ValidateProtocolJSON, ValidMinimalWebSocketIsValid) {
    json j = {
        {"id","abc"}, {"name","Test"}, {"transport","websocket"}, {"direction","receive"},
        {"wssPort",4269}, {"fields", json::array()}
    };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_TRUE(r.IsValid());
}

TEST(ValidateProtocolJSON, MissingOSCHostProducesWarningNotError) {
    json j = {
        {"id","abc"}, {"name","Test"}, {"transport","osc"}, {"direction","send"},
        {"fields", json::array()}
    };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_TRUE(r.IsValid());  // warning, not error
    EXPECT_FALSE(r.warnings.empty());
}

TEST(ValidateProtocolJSON, EmptyFieldsArrayProducesWarning) {
    json j = {
        {"id","abc"}, {"name","Test"}, {"transport","osc"}, {"direction","send"},
        {"oscHost","127.0.0.1"}, {"oscSendPort",9066},
        {"fields", json::array()}
    };
    auto r = ProtocolValidator::ValidateProtocolJSON(j);
    EXPECT_TRUE(r.IsValid());
    EXPECT_FALSE(r.warnings.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ValidateProtocolDefinition
// ═════════════════════════════════════════════════════════════════════════════

TEST(ValidateProtocolDefinition, ValidOSCDefinitionPasses) {
    ProtocolDefinition def;
    def.id        = "test-osc";
    def.name      = "Test OSC";
    def.transport = ProtocolTransport::OSC;
    def.direction = ProtocolDirection::Send;
    def.oscHost   = "127.0.0.1";
    def.oscSendPort = 9066;

    auto r = ProtocolValidator::ValidateProtocolDefinition(def);
    EXPECT_TRUE(r.IsValid());
}

TEST(ValidateProtocolDefinition, ValidWebSocketDefinitionPasses) {
    ProtocolDefinition def;
    def.id        = "test-ws";
    def.name      = "Test WS";
    def.transport = ProtocolTransport::WebSocket;
    def.direction = ProtocolDirection::Receive;
    def.wssPort   = 4269;

    auto r = ProtocolValidator::ValidateProtocolDefinition(def);
    EXPECT_TRUE(r.IsValid());
}

TEST(ValidateProtocolDefinition, InvalidPortIsError) {
    ProtocolDefinition def;
    def.id        = "bad-port";
    def.name      = "Bad Port";
    def.transport = ProtocolTransport::OSC;
    def.direction = ProtocolDirection::Send;
    def.oscHost   = "127.0.0.1";
    def.oscSendPort = 0;  // invalid

    auto r = ProtocolValidator::ValidateProtocolDefinition(def);
    EXPECT_FALSE(r.IsValid());
}

TEST(ValidateProtocolDefinition, EmptyIdIsError) {
    ProtocolDefinition def;
    def.id        = "";  // empty
    def.name      = "No ID";
    def.transport = ProtocolTransport::WebSocket;
    def.direction = ProtocolDirection::Send;
    def.wssPort   = 4269;

    auto r = ProtocolValidator::ValidateProtocolDefinition(def);
    EXPECT_FALSE(r.IsValid());
}