// WiFi Pineapple / Evil-Twin (rogue AP) beacon classifier.
// Pure, framework-free core ported from ESP32Marauder's PineScan logic
// (esp32_marauder/WiFiScan.cpp/.h) for the Bruce synthesis. No Arduino/ESP deps;
// unit-testable like RemoteIdDecoder.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Security conditions under which a vendor OUI is treated as suspicious.
enum PineScanSecurity : uint8_t {
    PINE_SEC_NONE = 0x00,
    PINE_SEC_WHEN_OPEN = 0x01,      // suspicious only if the AP is open
    PINE_SEC_WHEN_PROTECTED = 0x02, // suspicious only if the AP is protected
    PINE_SEC_ALWAYS = 0x04,         // always suspicious
};

struct PineScanVendor {
    const char *name;
    uint8_t securityFlags; // bitmask of PineScanSecurity
    uint32_t ouis[20];     // 24-bit OUIs (max 20 per vendor)
    uint8_t ouiCount;
};

extern const PineScanVendor kPineScanVendors[];
extern const int kPineScanVendorCount;

struct PineScanVerdict {
    bool suspicious = false;    // overall: rogue-AP / evil-twin candidate
    bool suspiciousOui = false; // matched a suspicious vendor OUI under its security condition
    bool tagAndSuspCap = false; // suspicious capability flags + truncated tag structure
    bool isOpen = false;        // AP advertises as open (no Privacy bit)
    int channel = -1;           // DS-Parameter channel from the beacon, or -1 if absent
    const char *vendorName = "Unknown";
};

// Classify an 802.11 beacon management frame (header + body, no radiotap, FCS may
// be trimmed). Returns suspicious=false for non-beacons / too-short frames.
PineScanVerdict pineScanClassify(const uint8_t *frame, size_t length);

// Helpers exposed for testing.
int pineScanExtractChannel(const uint8_t *frame, size_t length);   // DS-param channel, or -1
bool pineScanTagHeuristic(const uint8_t *frame, size_t length);    // truncated DS-param tag structure
