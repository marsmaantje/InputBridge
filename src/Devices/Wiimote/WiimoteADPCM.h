// src/Devices/Wiimote/WiimoteADPCM.h
//
// Encoder/decoder for SpeakerFormat::Adpcm4 (WiimoteProtocol.h). WiiBrew
// identifies this as standard Yamaha ADPCM, so the tables/logic here match
// ffmpeg's adpcm_yamaha codec - NOT the unrelated GameCube/Wii "DSP-ADPCM"
// used by .dsp/.brstm files.
//
// Pure sample-math only, no I/O - WiimoteDevice::QueueADPCM4() turns this
// into actual Report 0x18 writes.
//
// UNVERIFIED ON REAL HARDWARE: the nibble-packing order (first sample low
// nibble, second high) follows other open-source Wiimote drivers (WiiUse,
// CWiid), not anything WiiBrew states explicitly. If real hardware plays
// garbled/pitch-shifted audio, try swapping the nibble order in
// Encode()/DecodeByte() first.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace InputBridge::Wiimote {

// Stateful 4-bit Yamaha ADPCM encoder. Predictor/step persist across
// Encode() calls so incremental streams (multiple QueueADPCM4() calls)
// stay in sync with the hardware decoder's own running state - call
// Reset() after a hard stop where unsent audio was discarded.
class YamahaAdpcm4Encoder {
public:
    // Resets predictor=0, step=127 (matches decoder's documented startup).
    void Reset();

    // Encodes `count` signed 16-bit samples into `out`, 2 nibbles/byte
    // (low then high). An odd trailing sample is held until Flush().
    void Encode(const int16_t *pcm16, size_t count, std::vector<uint8_t> &out);

    // Emits a held odd trailing nibble (high nibble padded 0). No-op if
    // nothing is pending.
    void Flush(std::vector<uint8_t> &out);

private:
    void EncodeSample(int16_t sample, std::vector<uint8_t> &out);

    int32_t m_Predictor = 0;
    int32_t m_Step = 0; // 0 == "not yet started", see Reset()
    bool m_HasPendingNibble = false;
    uint8_t m_PendingNibble = 0;
};

// One-shot: encodes a full buffer (odd trailing sample auto-flushed) from
// a clean Reset() state. For incremental streams, use
// YamahaAdpcm4Encoder directly so state carries over between calls.
std::vector<uint8_t> EncodeYamahaAdpcm4(const int16_t *pcm16, size_t count);

// Decodes a packed nibble stream back to 16-bit PCM, from a clean Reset()
// state. Used by this file's round-trip unit tests.
std::vector<int16_t> DecodeYamahaAdpcm4(const uint8_t *packed, size_t byte_count);

} // namespace InputBridge::Wiimote
