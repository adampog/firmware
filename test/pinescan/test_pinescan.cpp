// Host unit tests for the pure PineScan (WiFi Pineapple / evil-twin) classifier.
// Built + run with plain g++ via .github/workflows/pinescan_native_test.yml
// (uses the same unity.h shim pattern as the Remote-ID tests).
#include <unity.h>

#include <cstring>

#include "pinescan_detector.h"

void setUp() {}
void tearDown() {}

// Build a minimal 802.11 beacon into f[64]; returns total length.
// capability is little-endian at offset 34; SA OUI at offset 10; SSID tag at 36;
// a DS-Parameter Set (channel) tag follows the SSID. If !dsLast, a trailing tag is
// appended so the DS tag is not the final one.
static size_t makeBeacon(uint8_t *f, uint32_t oui, uint16_t capab, uint8_t ssidLen, int channel, bool dsLast) {
    std::memset(f, 0, 64);
    f[0] = 0x80; // beacon
    f[10] = (oui >> 16) & 0xFF;
    f[11] = (oui >> 8) & 0xFF;
    f[12] = oui & 0xFF;
    f[34] = capab & 0xFF;
    f[35] = (capab >> 8) & 0xFF;
    f[36] = 0;       // SSID element id
    f[37] = ssidLen; // SSID length
    size_t p = 38 + ssidLen;
    f[p] = 3;
    f[p + 1] = 1;
    f[p + 2] = (uint8_t)channel; // DS Parameter Set
    size_t len = p + 3;
    if (!dsLast) {
        f[len] = 7; // some other tag (Country), len 1
        f[len + 1] = 1;
        f[len + 2] = 0;
        len += 3;
    }
    return len;
}

void test_always_suspicious_oui() {
    uint8_t f[64];
    size_t len = makeBeacon(f, 0x001337, 0x0011, 0, 6, false); // Orient Power, ALWAYS, protected
    PineScanVerdict v = pineScanClassify(f, len);
    TEST_ASSERT_TRUE(v.suspicious);
    TEST_ASSERT_TRUE(v.suspiciousOui);
    TEST_ASSERT_EQUAL_STRING("Orient Power Home Network Ltd", v.vendorName);
}

void test_when_open_oui_matches_only_when_open() {
    uint8_t f[64];
    size_t len = makeBeacon(f, 0x00C0CA, 0x0000, 0, 1, false); // Alfa WHEN_OPEN, open
    TEST_ASSERT_TRUE(pineScanClassify(f, len).suspicious);
    len = makeBeacon(f, 0x00C0CA, 0x0010, 0, 1, false); // protected -> not suspicious
    TEST_ASSERT_FALSE(pineScanClassify(f, len).suspicious);
}

void test_when_protected_oui_matches_only_when_protected() {
    uint8_t f[64];
    size_t len = makeBeacon(f, 0x02C0CA, 0x0010, 0, 11, false); // Hak5 WHEN_PROTECTED, protected
    TEST_ASSERT_TRUE(pineScanClassify(f, len).suspicious);
    len = makeBeacon(f, 0x02C0CA, 0x0000, 0, 11, false); // open -> not suspicious
    TEST_ASSERT_FALSE(pineScanClassify(f, len).suspicious);
}

void test_benign_oui_not_suspicious() {
    uint8_t f[64];
    size_t len = makeBeacon(f, 0xAABBCC, 0x0000, 0, 6, false);
    PineScanVerdict v = pineScanClassify(f, len);
    TEST_ASSERT_FALSE(v.suspicious);
    TEST_ASSERT_FALSE(v.suspiciousOui);
}

void test_extract_channel_from_ds_param() {
    uint8_t f[64];
    size_t len = makeBeacon(f, 0xAABBCC, 0x0000, 3, 9, false);
    TEST_ASSERT_EQUAL_INT32(9, pineScanExtractChannel(f, len));
}

void test_tag_and_susp_cap_flags_unknown_vendor() {
    uint8_t f[64];
    size_t len = makeBeacon(f, 0xAABBCC, 0x0001, 0, 6, true); // benign OUI, susp cap, truncated DS tag
    PineScanVerdict v = pineScanClassify(f, len);
    TEST_ASSERT_TRUE(v.tagAndSuspCap);
    TEST_ASSERT_TRUE(v.suspicious);
    TEST_ASSERT_FALSE(v.suspiciousOui);
    TEST_ASSERT_EQUAL_STRING("Unknown", v.vendorName);
}

void test_non_beacon_and_short_frames_ignored() {
    uint8_t f[64];
    size_t len = makeBeacon(f, 0x001337, 0x0011, 0, 6, false);
    f[0] = 0x50; // not a beacon subtype
    TEST_ASSERT_FALSE(pineScanClassify(f, len).suspicious);
    TEST_ASSERT_FALSE(pineScanClassify(f, 10).suspicious); // too short
    TEST_ASSERT_FALSE(pineScanClassify(nullptr, 0).suspicious);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_always_suspicious_oui);
    RUN_TEST(test_when_open_oui_matches_only_when_open);
    RUN_TEST(test_when_protected_oui_matches_only_when_protected);
    RUN_TEST(test_benign_oui_not_suspicious);
    RUN_TEST(test_extract_channel_from_ds_param);
    RUN_TEST(test_tag_and_susp_cap_flags_unknown_vendor);
    RUN_TEST(test_non_beacon_and_short_frames_ignored);
    return UNITY_END();
}
