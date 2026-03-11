#pragma once
#include "wheel/wheel.hpp"
#include "wheel/transport.hpp"
#include <memory>
#include <cstring>

namespace wheel {

// ---------------------------------------------------------------------------
// Moza HID shift-indicator LED report format (all bases: R3/R5/R9/R12/FSR)
//
// The wheelbase exposes a 64-byte HID output report.  SDL_hid_write expects
// the buffer to begin with the report ID byte (0x00 for single-report
// devices), so every write is 65 bytes: [reportID | 64 bytes payload].
//
// Payload layout (offsets into the 64-byte payload, i.e. buf[1..]):
//   [0]     0x07   – command page: steering-wheel LED update
//   [1]     0x10   – sub-command: set shift-indicator states
//   [2]     ledCount (uint8) – number of LED bytes that follow
//   [3..N]  per-LED value:  0x00 = off,  0xFF = full brightness
//   [N+1..] padding zeros
//
// The protocol was established by community HID sniffing of the Moza Pit
// House service; see: https://github.com/moza-racingwheel/moza-protocol
// ---------------------------------------------------------------------------

static constexpr uint8_t kMozaCmdPage    = 0x07;
static constexpr uint8_t kMozaCmdSetLEDs = 0x10;
static constexpr size_t  kMozaReportSize = 65; // 1 report-ID byte + 64 payload

class MozaBase : public Wheel {

protected:

    std::unique_ptr<HIDTransport> hid;
    int m_ledCount;

    MozaBase(std::unique_ptr<HIDTransport> t, int ledCount)
        : hid(std::move(t))
        , m_ledCount(ledCount)
    {}

    // Send a fully-formed LED state array to the device.
    // leds[i] == 0  →  LED off
    // leds[i] != 0  →  LED on at that brightness (0xFF = full)
    bool sendLEDs(const uint8_t* ledValues, size_t count)
    {
        uint8_t buf[kMozaReportSize]{};
        // buf[0] = 0x00 → HID report ID (already zero-initialised)
        buf[1] = kMozaCmdPage;
        buf[2] = kMozaCmdSetLEDs;
        buf[3] = static_cast<uint8_t>(count);

        for (size_t i = 0; i < count && i < (kMozaReportSize - 4); ++i)
            buf[4 + i] = ledValues[i];

        return hid->write(buf, kMozaReportSize);
    }
};

} // namespace wheel
