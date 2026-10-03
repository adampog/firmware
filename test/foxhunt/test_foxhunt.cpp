// Host unit tests for the pure fox-hunt / direction-finding core.
// Built + run with plain g++ via .github/workflows/foxhunt_native_test.yml
// (same unity.h shim pattern as the Remote-ID / PineScan tests).
#include <unity.h>

#include <cstring>

#include "fox_hunt_detector.h"

void setUp() {}
void tearDown() {}

// --- target matching / channel hop (ported from Marauder FoxHuntTarget) ---

void test_mac_matches() {
    uint8_t a[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    uint8_t b[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    uint8_t c[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02};
    TEST_ASSERT_TRUE(foxHuntMacMatches(a, b));
    TEST_ASSERT_FALSE(foxHuntMacMatches(a, c));
    TEST_ASSERT_FALSE(foxHuntMacMatches(nullptr, b));
    TEST_ASSERT_FALSE(foxHuntMacMatches(a, nullptr));
}

void test_should_update_channel() {
    TEST_ASSERT_TRUE(foxHuntShouldUpdateChannel(false, 6));
    TEST_ASSERT_FALSE(foxHuntShouldUpdateChannel(true, 6));  // bluetooth: no WiFi channel
    TEST_ASSERT_FALSE(foxHuntShouldUpdateChannel(false, 0)); // channel 0 = unknown
}

void test_target_stale_wrap_safe() {
    TEST_ASSERT_FALSE(foxHuntTargetIsStale(1000, 500, 1000)); // 500 <= timeout
    TEST_ASSERT_TRUE(foxHuntTargetIsStale(2000, 500, 1000));  // 1500 > timeout
    // unsigned wrap: now wrapped past 0, lastSeen just before wrap -> small delta
    TEST_ASSERT_FALSE(foxHuntTargetIsStale(50, 0xFFFFFFF0u, 1000));
}

void test_next_channel_round_robin() {
    TEST_ASSERT_EQUAL_UINT8(1, foxHuntNextChannel(0, 13));  // start
    TEST_ASSERT_EQUAL_UINT8(7, foxHuntNextChannel(6, 13));
    TEST_ASSERT_EQUAL_UINT8(1, foxHuntNextChannel(13, 13)); // wrap at max
    TEST_ASSERT_EQUAL_UINT8(1, foxHuntNextChannel(99, 13)); // beyond max wraps
    TEST_ASSERT_EQUAL_UINT8(0, foxHuntNextChannel(5, 0));   // no channels
}

// Build a minimal frame with the three 802.11 addresses populated.
static void makeFrame(uint8_t *f, const uint8_t ra[6], const uint8_t ta[6], const uint8_t bssid[6]) {
    std::memset(f, 0, 24);
    std::memcpy(f + 4, ra, 6);
    std::memcpy(f + 10, ta, 6);
    std::memcpy(f + 16, bssid, 6);
}

void test_frame_matches_any_address() {
    uint8_t target[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t other[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t f[24];

    makeFrame(f, target, other, other); // target as RA (addr1)
    TEST_ASSERT_TRUE(foxHuntFrameMatchesTarget(f, 24, target));
    makeFrame(f, other, target, other); // target as TA (addr2)
    TEST_ASSERT_TRUE(foxHuntFrameMatchesTarget(f, 24, target));
    makeFrame(f, other, other, target); // target as BSSID (addr3)
    TEST_ASSERT_TRUE(foxHuntFrameMatchesTarget(f, 24, target));
    makeFrame(f, other, other, other); // absent
    TEST_ASSERT_FALSE(foxHuntFrameMatchesTarget(f, 24, target));
    TEST_ASSERT_FALSE(foxHuntFrameMatchesTarget(f, 21, target)); // too short for addr3
}

// --- homing helpers ---

void test_smooth_rssi_converges() {
    // alpha 50%: halfway each step, with sign nudge so it reaches the sample.
    int ema = -90;
    ema = foxHuntSmoothRssi(ema, -40, 50); // -90 + (50*50/100) = -65
    TEST_ASSERT_EQUAL_INT32(-65, ema);
    ema = foxHuntSmoothRssi(ema, -40, 50); // -65 + (25*50/100 = 12) = -53
    TEST_ASSERT_EQUAL_INT32(-53, ema);
    // Converges exactly despite integer truncation (nudge of 1 when stuck).
    for (int i = 0; i < 20; ++i) ema = foxHuntSmoothRssi(ema, -40, 50);
    TEST_ASSERT_EQUAL_INT32(-40, ema);
    // alpha clamped to [1,100]; alpha 100 jumps straight to the sample.
    TEST_ASSERT_EQUAL_INT32(-30, foxHuntSmoothRssi(-80, -30, 100));
    TEST_ASSERT_EQUAL_INT32(-30, foxHuntSmoothRssi(-80, -30, 250));
}

void test_trend_deadband() {
    TEST_ASSERT_EQUAL_INT32(FOX_WARMER, foxHuntTrend(-70, -60, 3)); // +10 > 3
    TEST_ASSERT_EQUAL_INT32(FOX_COLDER, foxHuntTrend(-60, -72, 3)); // -12 > 3
    TEST_ASSERT_EQUAL_INT32(FOX_STEADY, foxHuntTrend(-60, -62, 3)); // within deadband
    TEST_ASSERT_EQUAL_INT32(FOX_STEADY, foxHuntTrend(-60, -60, 0));
}

void test_meter_level() {
    TEST_ASSERT_EQUAL_INT32(0, foxHuntMeterLevel(-95, -95, -30, 10));  // at floor
    TEST_ASSERT_EQUAL_INT32(0, foxHuntMeterLevel(-120, -95, -30, 10)); // below floor
    TEST_ASSERT_EQUAL_INT32(10, foxHuntMeterLevel(-30, -95, -30, 10)); // at ceil
    TEST_ASSERT_EQUAL_INT32(10, foxHuntMeterLevel(-10, -95, -30, 10)); // above ceil
    // Midpoint (-62.5) rounds to 5 of 10.
    TEST_ASSERT_EQUAL_INT32(5, foxHuntMeterLevel(-63, -95, -30, 10));
    // Degenerate args.
    TEST_ASSERT_EQUAL_INT32(0, foxHuntMeterLevel(-50, -95, -30, 0));
    TEST_ASSERT_EQUAL_INT32(0, foxHuntMeterLevel(-50, -30, -95, 10)); // inverted bounds
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_mac_matches);
    RUN_TEST(test_should_update_channel);
    RUN_TEST(test_target_stale_wrap_safe);
    RUN_TEST(test_next_channel_round_robin);
    RUN_TEST(test_frame_matches_any_address);
    RUN_TEST(test_smooth_rssi_converges);
    RUN_TEST(test_trend_deadband);
    RUN_TEST(test_meter_level);
    return UNITY_END();
}
