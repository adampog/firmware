// Drone Remote-ID (OpenDroneID) WiFi capture for Bruce.
// Capture glue around the pure RemoteIdDecoder/RemoteIdModel core (which was
// ported from ESP32Marauder). The promiscuous-capture + merge/lifecycle logic
// is adapted from ESP32Marauder's WiFiScanRemoteId.h to Bruce's WiFi stack.
//
// Gated behind -DBRUCE_REMOTEID (enabled on 8/16MB boards).
#pragma once

#ifdef BRUCE_REMOTEID

#include <stddef.h>
#include <stdint.h>

#include "RemoteIdModel.h"

// Run a blocking Remote-ID WiFi scan for durationMs (0 -> default ~30s), hopping
// 2.4GHz channels in passive promiscuous mode. Sets up and tears down the radio,
// prints a per-second heartbeat and a final detection dump to Serial, and leaves
// the detections queryable via remoteIdSnapshot(). Aborts early on any Serial input.
void remoteIdScanRun(uint32_t durationMs = 0);

// Thread-safe copy of the current detection store into `out` (up to `capacity`).
// Returns the number of records copied. Safe to call while a scan is running.
size_t remoteIdSnapshot(RemoteIdRecord *out, size_t capacity);

// Number of drones currently in the detection store.
size_t remoteIdDetectedCount();

// On-device live scan: renders a scrolling list of detected drones and runs until
// the user presses ESC (sets returnToMenu). Entry point for the WiFi menu.
void remoteIdScanScreen();

#endif // BRUCE_REMOTEID
