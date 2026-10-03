// RSSI-based fox-hunt / direction-finding core. See fox_hunt_detector.h.
// Matching/channel helpers ported verbatim from ESP32Marauder's FoxHuntTarget
// (esp32_marauder/FoxHuntTarget.cpp); homing helpers are new but equally pure.
#include "fox_hunt_detector.h"

bool foxHuntMacMatches(const uint8_t *target, const uint8_t *observed) {
    if (target == nullptr || observed == nullptr) return false;
    for (uint8_t i = 0; i < kFoxHuntMacSize; ++i) {
        if (target[i] != observed[i]) return false;
    }
    return true;
}

bool foxHuntShouldUpdateChannel(bool bluetooth, uint8_t channel) { return !bluetooth && channel > 0; }

bool foxHuntTargetIsStale(uint32_t now, uint32_t lastSeen, uint32_t timeout) {
    return static_cast<uint32_t>(now - lastSeen) > timeout;
}

uint8_t foxHuntNextChannel(uint8_t current, uint8_t maximum) {
    if (maximum == 0) return 0;
    if (current == 0 || current >= maximum) return 1;
    return current + 1;
}

bool foxHuntFrameMatchesTarget(const uint8_t *frame, size_t length, const uint8_t *target) {
    if (frame == nullptr || target == nullptr || length < 22) return false;
    // addr1 (RA) @4, addr2 (SA/TA) @10, addr3 (BSSID) @16.
    static const size_t kAddressOffsets[] = {4, 10, 16};
    for (size_t i = 0; i < 3; ++i) {
        if (foxHuntMacMatches(target, frame + kAddressOffsets[i])) return true;
    }
    return false;
}

int foxHuntSmoothRssi(int previousEma, int sample, int alphaPercent) {
    if (alphaPercent < 1) alphaPercent = 1;
    if (alphaPercent > 100) alphaPercent = 100;
    const int delta = sample - previousEma;
    int adjustment = (delta * alphaPercent) / 100; // truncates toward zero
    if (adjustment == 0 && delta != 0) adjustment = (delta > 0) ? 1 : -1;
    return previousEma + adjustment;
}

FoxHuntTrend foxHuntTrend(int previousRssi, int newRssi, int deadband) {
    if (deadband < 0) deadband = 0;
    if (newRssi - previousRssi > deadband) return FOX_WARMER;
    if (previousRssi - newRssi > deadband) return FOX_COLDER;
    return FOX_STEADY;
}

int foxHuntMeterLevel(int rssi, int rssiFloor, int rssiCeil, int levels) {
    if (levels <= 0 || rssiCeil <= rssiFloor) return 0;
    if (rssi <= rssiFloor) return 0;
    if (rssi >= rssiCeil) return levels;
    const long span = (long)rssiCeil - (long)rssiFloor;
    const long position = (long)rssi - (long)rssiFloor;
    return (int)((position * levels + span / 2) / span); // rounded to nearest bar
}
