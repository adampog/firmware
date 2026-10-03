// WiFi Pineapple / Evil-Twin beacon classifier. See pinescan_detector.h.
// Logic ported verbatim from ESP32Marauder's PineScan (WiFiScan.cpp/.h).
#include "pinescan_detector.h"

// Suspicious vendor OUIs (Pineapple/Hak5 and spoof-prone adapters). Ported from
// ESP32Marauder's WiFiScan::suspicious_vendors[].
const PineScanVendor kPineScanVendors[] = {
    {"Alfa Inc", PINE_SEC_WHEN_OPEN, {0x00C0CA}, 1},
    {"Orient Power Home Network Ltd", PINE_SEC_ALWAYS, {0x001337}, 1},
    {"Shenzhen Century Xinyang Technology Co Ltd", PINE_SEC_WHEN_OPEN, {0x1CBFCE}, 1},
    {"IEEE Registration Authority", PINE_SEC_WHEN_OPEN, {0x0CEFAF}, 1},
    {"Hak5", PINE_SEC_WHEN_PROTECTED, {0x02C0CA, 0x021337}, 2},
    {"MediaTek Inc", PINE_SEC_ALWAYS, {0x000A00, 0x000C43, 0x000CE7, 0x0017A5}, 4},
    {"Panda Wireless Inc", PINE_SEC_ALWAYS, {0x9CEFD5, 0x9CE5D5}, 2},
    {"Unassigned/Spoofed", PINE_SEC_ALWAYS, {0xDEADBE}, 1},
};
const int kPineScanVendorCount = sizeof(kPineScanVendors) / sizeof(kPineScanVendors[0]);

// Beacon layout: 24-byte mgmt header + 12-byte fixed params (timestamp[8],
// interval[2], capability[2]); tagged params start at offset 36, first tag is
// SSID (id 0) with length at offset 37.
int pineScanExtractChannel(const uint8_t *frame, size_t length) {
    if (frame == nullptr || length < 38) return -1;
    size_t pos = 36 + (size_t)frame[37] + 2; // skip SSID tag (36 + ssid_len + 2 hdr)
    while (pos + 2 <= length) {
        const uint8_t tagNum = frame[pos];
        const uint8_t tagLen = frame[pos + 1];
        if (pos + 2 + tagLen > length) break;
        if (tagNum == 3 && tagLen == 1) return frame[pos + 2]; // DS Parameter Set
        pos += tagLen + 2;
    }
    return -1;
}

// True when the DS-Parameter tag immediately follows the SSID and is the last tag
// in the frame (a truncated tag structure typical of some rogue beacons).
bool pineScanTagHeuristic(const uint8_t *frame, size_t length) {
    if (frame == nullptr || length < 38) return false;
    const size_t ssidLen = frame[37];
    const size_t pos = 36 + ssidLen + 2;
    if (pos + 2 <= length && frame[pos] == 3 && frame[pos + 1] == 1) {
        const size_t nextPos = pos + 2 + frame[pos + 1];
        return (nextPos >= length || nextPos + 2 > length);
    }
    return false;
}

PineScanVerdict pineScanClassify(const uint8_t *frame, size_t length) {
    PineScanVerdict v;
    if (frame == nullptr || length < 38) return v; // need header + SSID length byte
    if (frame[0] != 0x80) return v;                // beacon frames only

    v.channel = pineScanExtractChannel(frame, length);

    const uint16_t capab = (uint16_t)frame[34] | ((uint16_t)frame[35] << 8);
    const bool suspiciousCapability = (capab == 0x0001);
    const bool isProtected = (capab & 0x10) != 0;
    v.isOpen = !isProtected;

    v.tagAndSuspCap = suspiciousCapability && pineScanTagHeuristic(frame, length);

    const uint32_t oui =
        ((uint32_t)frame[10] << 16) | ((uint32_t)frame[11] << 8) | (uint32_t)frame[12];

    bool matched = false;
    for (int i = 0; i < kPineScanVendorCount && !matched; ++i) {
        const PineScanVendor &vd = kPineScanVendors[i];
        for (int j = 0; j < vd.ouiCount; ++j) {
            if (oui != vd.ouis[j]) continue;
            if ((vd.securityFlags & PINE_SEC_ALWAYS) ||
                (v.isOpen && (vd.securityFlags & PINE_SEC_WHEN_OPEN)) ||
                (isProtected && (vd.securityFlags & PINE_SEC_WHEN_PROTECTED))) {
                v.suspiciousOui = true;
                v.vendorName = vd.name;
                matched = true;
                break;
            }
        }
    }

    v.suspicious = v.suspiciousOui || v.tagAndSuspCap;
    if (v.tagAndSuspCap && !v.suspiciousOui) v.vendorName = "Unknown";
    return v;
}
