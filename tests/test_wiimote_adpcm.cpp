#include <gtest/gtest.h>
#include "Devices/Wiimote/WiimoteADPCM.h"
#include <cmath>
#include <vector>

using namespace InputBridge::Wiimote;

namespace {
std::vector<int16_t> MakeSine(float freq_hz, uint32_t sample_rate_hz, size_t count, float amplitude = 20000.0f) {
    std::vector<int16_t> out(count);
    for (size_t i = 0; i < count; ++i) {
        const float t = float(i) / float(sample_rate_hz);
        out[i] = int16_t(amplitude * std::sin(2.0f * 3.14159265f * freq_hz * t));
    }
    return out;
}
} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Packing shape
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteAdpcm, EvenCountPacksExactlyHalf) {
    auto pcm = MakeSine(440.0f, 3000, 40);
    auto packed = EncodeYamahaAdpcm4(pcm.data(), pcm.size());
    EXPECT_EQ(packed.size(), pcm.size() / 2);
}

TEST(WiimoteAdpcm, OddCountRoundsUpOneByte) {
    auto pcm = MakeSine(440.0f, 3000, 41);
    auto packed = EncodeYamahaAdpcm4(pcm.data(), pcm.size());
    EXPECT_EQ(packed.size(), (pcm.size() + 1) / 2);
}

TEST(WiimoteAdpcm, EmptyInputProducesNoOutput) {
    auto packed = EncodeYamahaAdpcm4(nullptr, 0);
    EXPECT_TRUE(packed.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Incremental encoding matches one-shot encoding
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteAdpcm, IncrementalCallsMatchOneShot) {
    auto pcm = MakeSine(880.0f, 3000, 137); // deliberately odd, deliberately not a multiple of any chunk size

    auto one_shot = EncodeYamahaAdpcm4(pcm.data(), pcm.size());

    YamahaAdpcm4Encoder enc;
    std::vector<uint8_t> incremental;
    // Feed it in ragged chunks (odd sizes on purpose) to exercise the
    // pending-nibble carry across Encode() calls.
    size_t pos = 0;
    for (size_t chunk : {7u, 13u, 1u, 20u, 40u, 56u}) {
        chunk = std::min(chunk, pcm.size() - pos);
        enc.Encode(pcm.data() + pos, chunk, incremental);
        pos += chunk;
    }
    enc.Flush(incremental);

    ASSERT_EQ(pos, pcm.size());
    EXPECT_EQ(incremental, one_shot);
}

// ═══════════════════════════════════════════════════════════════════════════
// Round trip: decode(encode(x)) should track x reasonably closely
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteAdpcm, RoundTripTracksInputSign) {
    // A slow enough tone relative to the sample rate that the 3-bit-
    // magnitude/step-size scheme (max slope ~7 steps/sample) can keep up
    // without slope-overload dominating the result - this isn't a
    // lossless codec, so the assertion below only checks that decoded
    // samples stay reasonably close to the source, not bit-exactness.
    const uint32_t rate = 8000;
    auto pcm = MakeSine(220.0f, rate, 400, 10000.0f);

    auto packed = EncodeYamahaAdpcm4(pcm.data(), pcm.size());
    auto decoded = DecodeYamahaAdpcm4(packed.data(), packed.size());

    ASSERT_EQ(decoded.size(), pcm.size());

    double sum_abs_err = 0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        sum_abs_err += std::abs(int(decoded[i]) - int(pcm[i]));
    }
    const double mean_abs_err = sum_abs_err / double(pcm.size());
    // Loose bound - this is checking "the codec broadly works", not
    // pinning down an exact quality figure.
    EXPECT_LT(mean_abs_err, 2000.0);
}

TEST(WiimoteAdpcm, SilenceRoundTripsToNearZero) {
    std::vector<int16_t> pcm(64, 0);
    auto packed = EncodeYamahaAdpcm4(pcm.data(), pcm.size());
    auto decoded = DecodeYamahaAdpcm4(packed.data(), packed.size());

    ASSERT_EQ(decoded.size(), pcm.size());
    for (int16_t s : decoded) {
        EXPECT_LT(std::abs(int(s)), 50);
    }
}

TEST(WiimoteAdpcm, DecodeEmptyProducesNoOutput) {
    auto decoded = DecodeYamahaAdpcm4(nullptr, 0);
    EXPECT_TRUE(decoded.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset() behavior
// ═══════════════════════════════════════════════════════════════════════════

TEST(WiimoteAdpcm, ResetMatchesFreshEncoder) {
    auto pcm = MakeSine(440.0f, 3000, 50);

    YamahaAdpcm4Encoder enc;
    std::vector<uint8_t> garbage;
    enc.Encode(pcm.data(), pcm.size(), garbage); // dirty the state
    enc.Flush(garbage);
    enc.Reset();

    std::vector<uint8_t> after_reset;
    enc.Encode(pcm.data(), pcm.size(), after_reset);
    enc.Flush(after_reset);

    auto fresh = EncodeYamahaAdpcm4(pcm.data(), pcm.size());
    EXPECT_EQ(after_reset, fresh);
}
