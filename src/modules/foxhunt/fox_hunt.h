// WiFi fox-hunt / radio-direction-finding capture for Bruce.
// Locks onto a target MAC and streams its RSSI (smoothed) + a warmer/colder
// proximity cue so an operator can home in on the transmitter. Promiscuous
// capture + WiFi init/teardown mirror src/modules/pinescan/pine_scan.cpp;
// matching/smoothing is the pure core in fox_hunt_detector.{h,cpp}.
// Gated behind -DBRUCE_FOXHUNT (8/16MB boards).
#pragma once

#ifdef BRUCE_FOXHUNT

#include <stddef.h>
#include <stdint.h>

// Headless hunt (serial): lock onto `target`, acquire/camp its channel, and
// stream RSSI + proximity meter + warmer/colder to Serial for durationMs
// (0 -> ~60s default). Aborts on Serial input.
void foxHuntRun(const uint8_t target[6], uint32_t durationMs = 0);

// On-device: pick a target AP from a quick scan, then a live homing screen
// (big RSSI, proximity bar, warmer/colder) until ESC. WiFi-menu entry.
void foxHuntScreen();

#endif // BRUCE_FOXHUNT
