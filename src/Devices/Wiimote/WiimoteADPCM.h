// src/Devices/Wiimote/WiimoteADPCM.h
//
// Encoder/decoder for the 4-bit ADPCM format selected by
// WiimoteProtocol.h's SpeakerFormat::Adpcm4 (register value 0x00).
// WiiBrew's Wiimote#Sound_Data_Format section identifies this specifically
// as "Yamaha ADPCM (for example, as implemented in ffmpeg)" rather than a
// Nintendo-specific scheme, so the tables and update rule below match
// ffmpeg's `adpcm_yamaha` codec (libavcodec/adpcm.c's
// adpcm_yamaha_expand_nibble() / adpcmenc.c's
// adpcm_yamaha_compress_sample()).
//
// IMPORTANT: this is NOT the same format as the GameCube/Wii "DSP-ADPCM"
// used by .dsp/.brstm files (an unrelated 8-coefficient linear-predictor
// scheme with 8-byte/14-sample frames) - don't reach for that algorithm or
// its tooling here, despite both being "the ADPCM Nintendo uses on Wii".
//
// This header intentionally contains ONLY pure sample-math - no I/O, no
// HID/report framing - matching WiimoteProtocol.h's "testable without a
// real device" design. WiimoteDevice.cpp owns turning this into actual
// Report 0x18 writes (see its QueueADPCM4()).
//
// UNVERIFIED ON REAL HARDWARE: unlike the 8-bit PCM path (confirmed
// working - see README.md's Speaker row), the nibble-packing order below
// (first sample of each pair in the low nibble, second in the high
// nibble) follows the convention used by other open-source Wiimote
// drivers (WiiUse, CWiid) rather than anything WiiBrew states explicitly.
// If real-hardware testing finds audio that's present but garbled/
// pitch-shifted rather than absent, swapping the nibble order in
// Encode()/DecodeByte() below is the first thing to try.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace InputBridge::Wiimote {

// Stateful 4-bit Yamaha ADPCM encoder. Predictor/step state persists
// across Encode() calls so a stream can be fed incrementally (e.g. from
// several QueueADPCM4() calls) and still decode correctly as one
// continuous signal on the Wiimote's end - restarting the state cold on
// every call would desync from wherever the hardware decoder's own
// running prediction actually is, since (unlike 8-bit PCM, where every
// sample stands alone) each ADPCM nibble only makes sense relative to
// that running state. Callers that need a hard reset (e.g. because
// playback was stopped and unsent audio discarded, leaving the actual
// hardware decoder's state unknown) should call Reset().
class YamahaAdpcm4Encoder {
public:
    // Returns the encoder to its startup state. step==0 is this class's
    // (and ffmpeg's) sentinel for "not yet started" - the first sample
    // encoded after Reset() always resets predictor=0, step=127 before
    // encoding, matching the decoder's own documented startup state so a
    // freshly (re)configured speaker and a freshly Reset() encoder agree.
    void Reset();

    // Encodes `count` signed 16-bit PCM samples, appending one 4-bit code
    // per sample to `out`, packed two-per-byte (first sample of each pair
    // in bits 0-3, second in bits 4-7 - see the UNVERIFIED note above). If
    // `count` is odd, the final nibble is held internally rather than
    // padded into `out`; call Flush() once the stream is finished to emit
    // it.
    void Encode(const int16_t *pcm16, size_t count, std::vector<uint8_t> &out);

    // Emits a held odd trailing nibble, if any, padding the byte's high
    // nibble with 0. No-op if the total sample count encoded so far is
    // even. Safe to call defensively even when nothing is pending.
    void Flush(std::vector<uint8_t> &out);

private:
    void EncodeSample(int16_t sample, std::vector<uint8_t> &out);

    int32_t m_Predictor = 0;
    int32_t m_Step = 0; // 0 == "not yet started", see Reset()
    bool m_HasPendingNibble = false;
    uint8_t m_PendingNibble = 0;
};

// One-shot convenience: encodes a full buffer (including any odd trailing
// sample, auto-flushed) into a freshly allocated packed nibble stream,
// starting from a clean Reset() state. For continuous/incremental
// streams, use YamahaAdpcm4Encoder directly instead so state carries over
// between calls.
std::vector<uint8_t> EncodeYamahaAdpcm4(const int16_t *pcm16, size_t count);

// Decodes a packed nibble stream (as produced by YamahaAdpcm4Encoder) back
// to 16-bit PCM, starting from the same Reset() state. Included primarily
// so this file's own round-trip unit tests can verify the encoder without
// a real Wiimote; also useful if this codebase ever wants to preview
// audio before sending it.
std::vector<int16_t> DecodeYamahaAdpcm4(const uint8_t *packed, size_t byte_count);

} // namespace InputBridge::Wiimote
