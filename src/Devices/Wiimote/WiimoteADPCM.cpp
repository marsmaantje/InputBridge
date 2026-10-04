// src/Devices/Wiimote/WiimoteADPCM.cpp
#include "WiimoteADPCM.h"
#include <algorithm>
#include <array>

namespace InputBridge::Wiimote {

namespace {

// ffmpeg's yamaha_indexscale[]/yamaha_difflookup[] (see WiimoteADPCM.h).
// Indexed by the full 4-bit code (sign 0x8 | 3-bit magnitude); both halves
// match since the sign bit only affects delta direction, not step size.
constexpr std::array<int, 16> kIndexScale = {
    230, 230, 230, 230, 307, 409, 512, 614,
    230, 230, 230, 230, 307, 409, 512, 614,
};
constexpr std::array<int, 16> kDiffLookup = {
    1, 3, 5, 7, 9, 11, 13, 15,
    -1, -3, -5, -7, -9, -11, -13, -15,
};

int32_t ClipInt16(int32_t v) {
    return std::clamp(v, int32_t(-32768), int32_t(32767));
}

int32_t ClipStep(int32_t v) {
    return std::clamp(v, int32_t(127), int32_t(24576));
}

} // namespace

void YamahaAdpcm4Encoder::Reset() {
    m_Predictor = 0;
    m_Step = 0;
    m_HasPendingNibble = false;
    m_PendingNibble = 0;
}

void YamahaAdpcm4Encoder::EncodeSample(int16_t sample, std::vector<std::byte> &out) {
    if (m_Step == 0) {
        // First sample since construction/Reset() - see Reset()'s comment.
        m_Predictor = 0;
        m_Step = 127;
    }

    int32_t delta = static_cast<int32_t>(sample) - m_Predictor;
    std::byte nibble = delta < 0 ? std::byte{0x8} : std::byte{0x0};
    if (std::to_integer<unsigned>(nibble)) delta = -delta;
    // 3-bit magnitude: quarter-steps the delta spans, capped at 7 (this
    // cap is the codec's lossy part - standard ADPCM slope overload).
    nibble = static_cast<std::byte>(std::to_integer<unsigned>(nibble) | std::min(7, (delta * 4) / m_Step));

    // Update from the emitted CODE, not the raw delta, to stay in lockstep
    // with a decoder that only ever sees 4-bit codes.
    const auto code = static_cast<uint8_t>(std::to_integer<unsigned>(nibble));
    const auto code_index = static_cast<size_t>(code);
    m_Predictor = static_cast<int16_t>(ClipInt16(m_Predictor + (m_Step * kDiffLookup[code_index]) / 8));
    m_Step = ClipStep((m_Step * kIndexScale[code_index]) >> 8);

    if (m_HasPendingNibble) {
        const auto pending = static_cast<std::byte>(m_PendingNibble);
        const auto combined = static_cast<std::byte>(
            static_cast<unsigned>(pending) | (static_cast<unsigned>(nibble) << 4));
        out.push_back(combined);
        m_HasPendingNibble = false;
    } else {
        m_PendingNibble = static_cast<uint8_t>(std::to_integer<unsigned>(nibble));
        m_HasPendingNibble = true;
    }
}

void YamahaAdpcm4Encoder::Encode(const int16_t *pcm16, size_t count, std::vector<std::byte> &out) {
    if (!pcm16 || !count) return;
    out.reserve(out.size() + (count + 1) / 2);
    for (size_t i = 0; i < count; ++i) EncodeSample(pcm16[i], out);
}

void YamahaAdpcm4Encoder::Flush(std::vector<std::byte> &out) {
    if (m_HasPendingNibble) {
        out.push_back(static_cast<std::byte>(m_PendingNibble)); // high nibble padded with 0
        m_HasPendingNibble = false;
    }
}

std::vector<std::byte> EncodeYamahaAdpcm4(const int16_t *pcm16, size_t count) {
    YamahaAdpcm4Encoder enc;
    std::vector<std::byte> out;
    enc.Encode(pcm16, count, out);
    enc.Flush(out);
    return out;
}

std::vector<int16_t> DecodeYamahaAdpcm4(std::span<const std::byte> packed) {
    std::vector<int16_t> out;
    if (packed.empty()) return out;
    out.reserve(packed.size() * 2);

    int32_t predictor = 0;
    int32_t step = 0;

    // Mirrors EncodeSample()'s reset-on-first-sample and update rule
    // exactly, just reading nibbles instead of deriving them from PCM.
    auto decode_nibble = [&](uint8_t nibble) {
        if (step == 0) {
            predictor = 0;
            step = 127;
        }
        const auto code = static_cast<std::byte>(nibble) & std::byte{0x0F};
        const auto code_value = std::to_integer<unsigned>(code);
        predictor = ClipInt16(predictor + (step * kDiffLookup[code_value]) / 8);
        step = ClipStep((step * kIndexScale[code_value]) >> 8);
        out.push_back(static_cast<int16_t>(predictor));
    };

    for (const std::byte b : packed) {
        const std::byte low_nibble = b & std::byte{0x0Fu};
        const std::byte high_nibble = (b >> 4) & std::byte{0x0Fu};
        decode_nibble(static_cast<uint8_t>(std::to_integer<unsigned>(low_nibble)));  // low nibble first
        decode_nibble(static_cast<uint8_t>(std::to_integer<unsigned>(high_nibble))); // then high nibble
    }
    return out;
}

} // namespace InputBridge::Wiimote
