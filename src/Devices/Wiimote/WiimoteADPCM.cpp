// src/Devices/Wiimote/WiimoteADPCM.cpp
#include "WiimoteADPCM.h"
#include <algorithm>

namespace InputBridge::Wiimote {

namespace {

// ffmpeg's yamaha_indexscale[]/yamaha_difflookup[] (see WiimoteADPCM.h).
// Indexed by the full 4-bit code (sign 0x8 | 3-bit magnitude); both halves
// match since the sign bit only affects delta direction, not step size.
constexpr int kIndexScale[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    230, 230, 230, 230, 307, 409, 512, 614,
};
constexpr int kDiffLookup[16] = {
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

void YamahaAdpcm4Encoder::EncodeSample(int16_t sample, std::vector<uint8_t> &out) {
    if (m_Step == 0) {
        // First sample since construction/Reset() - see Reset()'s comment.
        m_Predictor = 0;
        m_Step = 127;
    }

    int32_t delta = int32_t(sample) - m_Predictor;
    uint8_t nibble = delta < 0 ? 0x8 : 0x0;
    if (nibble) delta = -delta;
    // 3-bit magnitude: quarter-steps the delta spans, capped at 7 (this
    // cap is the codec's lossy part - standard ADPCM slope overload).
    nibble = uint8_t(nibble | std::min(7, (delta * 4) / m_Step));

    // Update from the emitted CODE, not the raw delta, to stay in lockstep
    // with a decoder that only ever sees 4-bit codes.
    m_Predictor = int16_t(ClipInt16(m_Predictor + (m_Step * kDiffLookup[nibble]) / 8));
    m_Step = ClipStep((m_Step * kIndexScale[nibble]) >> 8);

    if (m_HasPendingNibble) {
        out.push_back(uint8_t(m_PendingNibble | (nibble << 4)));
        m_HasPendingNibble = false;
    } else {
        m_PendingNibble = nibble;
        m_HasPendingNibble = true;
    }
}

void YamahaAdpcm4Encoder::Encode(const int16_t *pcm16, size_t count, std::vector<uint8_t> &out) {
    if (!pcm16 || !count) return;
    out.reserve(out.size() + (count + 1) / 2);
    for (size_t i = 0; i < count; ++i) EncodeSample(pcm16[i], out);
}

void YamahaAdpcm4Encoder::Flush(std::vector<uint8_t> &out) {
    if (m_HasPendingNibble) {
        out.push_back(m_PendingNibble); // high nibble padded with 0
        m_HasPendingNibble = false;
    }
}

std::vector<uint8_t> EncodeYamahaAdpcm4(const int16_t *pcm16, size_t count) {
    YamahaAdpcm4Encoder enc;
    std::vector<uint8_t> out;
    enc.Encode(pcm16, count, out);
    enc.Flush(out);
    return out;
}

std::vector<int16_t> DecodeYamahaAdpcm4(const uint8_t *packed, size_t byte_count) {
    std::vector<int16_t> out;
    if (!packed || !byte_count) return out;
    out.reserve(byte_count * 2);

    int32_t predictor = 0;
    int32_t step = 0;

    // Mirrors EncodeSample()'s reset-on-first-sample and update rule
    // exactly, just reading nibbles instead of deriving them from PCM.
    auto decode_nibble = [&](uint8_t nibble) {
        if (step == 0) {
            predictor = 0;
            step = 127;
        }
        predictor = ClipInt16(predictor + (step * kDiffLookup[nibble & 0xF]) / 8);
        step = ClipStep((step * kIndexScale[nibble & 0xF]) >> 8);
        out.push_back(int16_t(predictor));
    };

    for (size_t i = 0; i < byte_count; ++i) {
        decode_nibble(uint8_t(packed[i] & 0x0F));       // low nibble first
        decode_nibble(uint8_t((packed[i] >> 4) & 0x0F)); // then high nibble
    }
    return out;
}

} // namespace InputBridge::Wiimote
