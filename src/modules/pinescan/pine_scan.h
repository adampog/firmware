// WiFi Pineapple / Evil-Twin (rogue AP) detector capture for Bruce.
// Promiscuous beacon capture around the pure pinescan_detector classifier.
// Gated behind -DBRUCE_PINESCAN (8/16MB boards).
#pragma once

#ifdef BRUCE_PINESCAN

#include <stddef.h>
#include <stdint.h>

// Blocking passive scan for durationMs (0 -> ~30s default): hops 2.4GHz channels,
// classifies beacons, and prints suspicious APs to Serial. Aborts on Serial input.
void pineScanRun(uint32_t durationMs = 0);

// On-device live scan: scrolling list of suspicious APs; runs until ESC. WiFi-menu entry.
void pineScanScreen();

// Number of distinct suspicious APs currently detected.
size_t pineScanHitCount();

#endif // BRUCE_PINESCAN
