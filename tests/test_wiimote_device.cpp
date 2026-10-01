// Tests WiimoteDevice's wire format and report handling through a scripted
// IWiimoteTransport - no SDL_hid I/O, no hardware. Pins the exact bytes that
// go out (report IDs, register address/size framing, rumble-bit merging) and
// that incoming reports decode into the snapshot, so the std::byte refactor
// of the raw-HID layer can't silently change the protocol.
#include <gtest/gtest.h>
#include "Devices/Wiimote/WiimoteDevice.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <vector>

using namespace InputBridge::Wiimote;

namespace {

using Report = std::vector<std::byte>;

template <typename... Ts>
Report Bytes(Ts... values) { return {static_cast<std::byte>(values)...}; }

// Records every Write(); Read() pops scripted reports. Shared state lives in
// a struct the test keeps a pointer to, since WiimoteDevice owns the transport.
struct MockState {
    std::vector<Report> writes;
    std::deque<Report> reads;
    bool open = true;
};

class MockTransport : public IWiimoteTransport {
public:
    explicit MockTransport(MockState *s) : m_S(s) {}
    bool IsOpen() const override { return m_S->open; }
    int Write(std::span<const std::byte> data) override {
        m_S->writes.emplace_back(data.begin(), data.end());
        return static_cast<int>(data.size());
    }
    int Read(std::span<std::byte> buf) override {
        if (m_S->reads.empty()) return 0;
        const Report r = m_S->reads.front();
        m_S->reads.pop_front();
        const std::size_t n = std::min(r.size(), buf.size());
        std::copy_n(r.begin(), n, buf.begin());
        return static_cast<int>(n);
    }
    void Close() override { m_S->open = false; }
private:
    MockState *m_S;
};

struct Fixture : ::testing::Test {
    MockState state;
    std::unique_ptr<WiimoteDevice> dev;
    void SetUp() override {
        dev = std::make_unique<WiimoteDevice>(std::make_unique<MockTransport>(&state), "mock", false);
    }
};

} // namespace

TEST_F(Fixture, LedMaskSendsReport11WithMaskByte) {
    dev->SetLEDMask(0x30);
    ASSERT_EQ(state.writes.size(), 1u);
    const Report &w = state.writes[0];
    ASSERT_GE(w.size(), 2u);
    EXPECT_EQ(w[0], std::byte{0x11});
    EXPECT_EQ(w[1], std::byte{0x30});
    EXPECT_EQ(dev->Snapshot().led_mask, 0x30);
}

TEST_F(Fixture, PlayerLedMapsPlayerNumberToHighNibble) {
    dev->SetPlayerLED(1);
    dev->SetPlayerLED(4);
    dev->SetPlayerLED(99); // clamped to player 4
    ASSERT_EQ(state.writes.size(), 3u);
    EXPECT_EQ(state.writes[0][1], std::byte{0x10});
    EXPECT_EQ(state.writes[1][1], std::byte{0x80});
    EXPECT_EQ(state.writes[2][1], std::byte{0x80});
}

TEST_F(Fixture, WriteRegisterFramesAddressSizeAndPayload) {
    const std::array payload{std::byte{0xAA}, std::byte{0x55}};
    ASSERT_TRUE(dev->WriteRegister(0xA400F1, payload));
    ASSERT_EQ(state.writes.size(), 1u);
    const Report &w = state.writes[0];
    ASSERT_GE(w.size(), 8u);
    EXPECT_EQ(w[0], std::byte{0x16});  // WriteMemory
    EXPECT_EQ(w[1], std::byte{0x04});  // control-register flag, rumble bit clear
    EXPECT_EQ(w[2], std::byte{0xA4});
    EXPECT_EQ(w[3], std::byte{0x00});
    EXPECT_EQ(w[4], std::byte{0xF1});
    EXPECT_EQ(w[5], std::byte{0x02});  // size
    EXPECT_EQ(w[6], std::byte{0xAA});
    EXPECT_EQ(w[7], std::byte{0x55});
}

TEST_F(Fixture, WriteRegisterRejectsMoreThan16Bytes) {
    const std::array<std::byte, 17> tooBig{};
    EXPECT_FALSE(dev->WriteRegister(0xA40000, tooBig));
    EXPECT_TRUE(state.writes.empty());
}

TEST_F(Fixture, ReadRegisterSendsRequestAndCopiesReply) {
    // Reply: 0x21, buttons(2), SE = (size-1)<<4 | err, addr(2), data...
    state.reads.push_back(Bytes(0x21, 0, 0, 0x50, 0xFA, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66));
    std::array<std::byte, 6> out{};
    ASSERT_TRUE(dev->ReadRegister(0xA400FA, out));

    ASSERT_EQ(state.writes.size(), 1u);
    const Report &w = state.writes[0];
    ASSERT_GE(w.size(), 7u);
    EXPECT_EQ(w[0], std::byte{0x17}); // ReadMemory
    EXPECT_EQ(w[1], std::byte{0x04});
    EXPECT_EQ(w[2], std::byte{0xA4});
    EXPECT_EQ(w[3], std::byte{0x00});
    EXPECT_EQ(w[4], std::byte{0xFA});
    EXPECT_EQ(w[5], std::byte{0x00}); // size hi
    EXPECT_EQ(w[6], std::byte{0x06}); // size lo

    const std::array expected{std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
                              std::byte{0x44}, std::byte{0x55}, std::byte{0x66}};
    EXPECT_EQ(out, expected);
}

TEST_F(Fixture, ReadRegisterFailsOnErrorNibble) {
    state.reads.push_back(Bytes(0x21, 0, 0, 0x57, 0x00, 0x00, 0, 0, 0, 0, 0, 0));
    std::array<std::byte, 6> out{};
    EXPECT_FALSE(dev->ReadRegister(0xA400FA, out));
}

TEST_F(Fixture, ReadRegisterSplitsReadsLargerThan16Bytes) {
    std::array<std::byte, 20> out{};
    Report first = Bytes(0x21, 0, 0, 0xF0, 0x00, 0x20);
    first.resize(6 + 16, std::byte{0x01});
    Report second = Bytes(0x21, 0, 0, 0x30, 0x00, 0x30, 0x02, 0x02, 0x02, 0x02);
    state.reads.push_back(first);
    state.reads.push_back(second);
    ASSERT_TRUE(dev->ReadRegister(0xA40020, out));
    ASSERT_EQ(state.writes.size(), 2u);
    EXPECT_EQ(state.writes[1][4], std::byte{0x30}); // second chunk at +0x10
    EXPECT_EQ(out[0], std::byte{0x01});
    EXPECT_EQ(out[15], std::byte{0x01});
    EXPECT_EQ(out[16], std::byte{0x02});
    EXPECT_EQ(out[19], std::byte{0x02});
}

TEST_F(Fixture, PollDecodesButtonsAccelAndIrFromReport37) {
    Report r(22, std::byte{0});
    r[0] = std::byte{0x37};
    r[1] = std::byte{0x00};            // BB0
    r[2] = std::byte{0x08};            // BB1: A
    r[3] = std::byte{0x80};            // AX
    r[4] = std::byte{0x80};
    r[5] = std::byte{0x80};
    r[6] = std::byte{0x11};            // IR dot0 X low
    r[7] = std::byte{0x22};            // IR dot0 Y low
    state.reads.push_back(r);
    dev->Poll();
    const auto &s = dev->Snapshot();
    EXPECT_TRUE(s.connected);
    EXPECT_TRUE(s.core.a);
    EXPECT_FALSE(s.core.b);
    EXPECT_EQ(s.accel.raw_x >> 2, 0x80);
    EXPECT_EQ(s.ir[0].x & 0xFF, 0x11);
}

TEST_F(Fixture, PollIgnoresTruncatedReport37) {
    Report r(10, std::byte{0});
    r[0] = std::byte{0x37};
    r[2] = std::byte{0x08};
    state.reads.push_back(r);
    dev->Poll();
    EXPECT_FALSE(dev->Snapshot().core.a); // too short to trust - not decoded
}

TEST_F(Fixture, PollSurvivesTruncatedStatusReport) {
    state.reads.push_back(Bytes(0x20, 0x00, 0x00)); // status needs 7 bytes
    EXPECT_NO_FATAL_FAILURE(dev->Poll());
}

TEST_F(Fixture, StatusReportClassifiesBattery) {
    state.reads.push_back(Bytes(0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90));
    dev->Poll();
    EXPECT_EQ(dev->Snapshot().battery, BatteryBars::Four);
}

TEST_F(Fixture, StatusReportReassertsDataReportMode) {
    state.reads.push_back(Bytes(0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90));
    dev->Poll();
    const auto it = std::find_if(state.writes.begin(), state.writes.end(),
                                 [](const Report &w) { return w[0] == std::byte{0x12}; });
    ASSERT_NE(it, state.writes.end());
    EXPECT_EQ((*it)[1], std::byte{0x04});
    EXPECT_EQ((*it)[2], std::byte{0x37});
}

TEST_F(Fixture, RumbleBitIsMergedIntoOtherReports) {
    dev->SetRumble(1.0f);          // full intensity -> bit on
    state.writes.clear();
    dev->SetLEDMask(0x10);
    ASSERT_EQ(state.writes.size(), 1u);
    EXPECT_EQ(state.writes[0][1], std::byte{0x11}); // mask | rumble bit
}
