// Shared WiFi band plan for Bruce (SYNTHESIS §3 port #4, dual-band / 5GHz).
// Pure, framework-free band classification + channel-frequency math + the
// channel hop lists used by the passive scan modules. No Arduino/ESP deps, so
// it is unit-testable like the Remote-ID / PineScan / fox-hunt cores.
//
// ESP32-C5 is a 2.4 + 5 GHz part; on the C5 (build flag -DBRUCE_DUALBAND) the
// scan modules walk the dual-band plan, which tunes 5 GHz channels by calling
// esp_wifi_set_channel() directly (the C5 IDF derives the band from the channel
// number, as ESP32Marauder does). Every other board stays 2.4 GHz only.
//
// Names are BRUCE_-prefixed to avoid clashing with IDF's wifi_band_t /
// WIFI_BAND_* in translation units that include both this and esp_wifi.h.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum BruceWifiBand : uint8_t {
    BRUCE_BAND_24 = 0,      // 2.4 GHz (channels 1..14)
    BRUCE_BAND_5 = 1,       // 5 GHz (channels 36..177)
    BRUCE_BAND_UNKNOWN = 2, // not a recognised WiFi channel
};

// Classify an 802.11 channel number into a band.
BruceWifiBand bruceBandForChannel(int channel);

// Center frequency (MHz) for a channel, or 0 if the channel is not recognised.
int bruceChannelToFreqMhz(int channel);

// True when `channel` belongs to `band`.
bool bruceChannelInBand(int channel, BruceWifiBand band);

// 2.4 GHz hop plan (1/6/11 first, then the rest) and the dual-band plan that
// interleaves 2.4 GHz with the common non-DFS + lower-DFS 5 GHz channels.
extern const uint8_t kBruceChannels24[];
extern const size_t kBruceChannels24Count;
extern const uint8_t kBruceChannelsDualBand[];
extern const size_t kBruceChannelsDualBandCount;

// The active hop plan for this build: dual-band when -DBRUCE_DUALBAND is set
// (ESP32-C5), otherwise 2.4 GHz only. Header-inline so it resolves per build
// without a flag branch living in the pure .cpp.
inline const uint8_t *bruceScanChannelPlan(size_t *count) {
#ifdef BRUCE_DUALBAND
    *count = kBruceChannelsDualBandCount;
    return kBruceChannelsDualBand;
#else
    *count = kBruceChannels24Count;
    return kBruceChannels24;
#endif
}
