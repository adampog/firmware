// Shared WiFi band plan. See band_plan.h. Pure logic + channel tables; no ifdefs
// here so the host tests exercise the same code the firmware compiles.
#include "band_plan.h"

// 2.4 GHz: 1/6/11 (non-overlapping) first, then the remaining channels.
const uint8_t kBruceChannels24[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
const size_t kBruceChannels24Count = sizeof(kBruceChannels24) / sizeof(kBruceChannels24[0]);

// Dual-band: 2.4 GHz anchors + 5 GHz UNII-1 (36-48) and UNII-3 (149-165)
// non-DFS, then UNII-2A (52-64) DFS which is legal to listen on passively.
const uint8_t kBruceChannelsDualBand[] = {
    1,  6,  11,                  // 2.4 GHz anchors
    36, 40, 44,  48,             // UNII-1
    149, 153, 157, 161, 165,     // UNII-3
    52, 56, 60, 64,              // UNII-2A (DFS, passive RX only)
};
const size_t kBruceChannelsDualBandCount =
    sizeof(kBruceChannelsDualBand) / sizeof(kBruceChannelsDualBand[0]);

BruceWifiBand bruceBandForChannel(int channel) {
    if (channel >= 1 && channel <= 14) return BRUCE_BAND_24;
    if (channel >= 36 && channel <= 177) return BRUCE_BAND_5;
    return BRUCE_BAND_UNKNOWN;
}

int bruceChannelToFreqMhz(int channel) {
    if (channel >= 1 && channel <= 13) return 2412 + (channel - 1) * 5;
    if (channel == 14) return 2484; // special-cased in 802.11
    if (channel >= 36 && channel <= 177) return 5000 + channel * 5;
    return 0;
}

bool bruceChannelInBand(int channel, BruceWifiBand band) {
    return bruceBandForChannel(channel) == band;
}
