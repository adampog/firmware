// RSSI-based fox-hunt / radio-direction-finding core for Bruce.
// Pure, framework-free logic ported from ESP32Marauder's FoxHuntTarget
// (esp32_marauder/FoxHuntTarget.{h,cpp}) plus homing helpers (RSSI smoothing,
// proximity meter, warmer/colder trend). No Arduino/ESP deps, so it is
// unit-testable like RemoteIdDecoder / pinescan_detector.
#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint8_t kFoxHuntMacSize = 6;

// --- target matching / channel hop (ported verbatim from Marauder FoxHuntTarget) ---

// True when the two 6-byte MACs are equal. Null pointers never match.
bool foxHuntMacMatches(const uint8_t *target, const uint8_t *observed);

// Channel is only re-latched from WiFi frames; BT advertisements carry no WiFi channel.
bool foxHuntShouldUpdateChannel(bool bluetooth, uint8_t channel);

// True when now - lastSeen exceeds timeout (unsigned-wrap safe).
bool foxHuntTargetIsStale(uint32_t now, uint32_t lastSeen, uint32_t timeout);

// Next channel in a 1..maximum round-robin (maximum 0 -> 0). Used to acquire a
// target whose channel is not yet known.
uint8_t foxHuntNextChannel(uint8_t current, uint8_t maximum);

// True when `target` appears as any of the three 802.11 addresses (addr1/addr2/
// addr3 at offsets 4/10/16) of a management or data frame. `length` must cover
// addr3 (>= 22). A target may be the transmitter, receiver, or BSSID depending
// on frame direction, so all three are checked (mirrors Marauder's SIG_STREN path).
bool foxHuntFrameMatchesTarget(const uint8_t *frame, size_t length, const uint8_t *target);

// --- homing helpers (turn RSSI into human-facing direction cues) ---

// Integer exponential moving average: result = previousEma + (sample-previousEma)
// * alphaPercent/100, rounded toward the sample so it always converges (never
// stalls 1 dB short). Seed previousEma with the first sample on acquisition.
// alphaPercent is clamped to 1..100.
int foxHuntSmoothRssi(int previousEma, int sample, int alphaPercent);

enum FoxHuntTrend : int8_t {
    FOX_COLDER = -1, // signal weaker -> moving away from the target
    FOX_STEADY = 0,  // within the dead band
    FOX_WARMER = 1,  // signal stronger -> getting closer
};

// Compares newRssi to previousRssi with a +/- deadband (dB) guard band so small
// fluctuations read as STEADY rather than flapping.
FoxHuntTrend foxHuntTrend(int previousRssi, int newRssi, int deadband);

// Maps rssi (dBm) into 0..levels proximity bars. rssi <= rssiFloor -> 0,
// rssi >= rssiCeil -> levels, linear (rounded) in between. rssiFloor is the
// far/weak bound (e.g. -95), rssiCeil the near/strong bound (e.g. -30).
int foxHuntMeterLevel(int rssi, int rssiFloor, int rssiCeil, int levels);
