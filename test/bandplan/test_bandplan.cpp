// Host unit tests for the pure WiFi band-plan core (src/modules/wifi/band_plan).
// Built + run with plain g++ via .github/workflows/bandplan_native_test.yml.
#include <unity.h>

#include "band_plan.h"

void setUp() {}
void tearDown() {}

void test_band_for_channel() {
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_24, bruceBandForChannel(1));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_24, bruceBandForChannel(14));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_5, bruceBandForChannel(36));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_5, bruceBandForChannel(165));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_5, bruceBandForChannel(177));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_UNKNOWN, bruceBandForChannel(0));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_UNKNOWN, bruceBandForChannel(15));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_UNKNOWN, bruceBandForChannel(35));
    TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_UNKNOWN, bruceBandForChannel(200));
}

void test_channel_to_freq() {
    TEST_ASSERT_EQUAL_INT32(2412, bruceChannelToFreqMhz(1));
    TEST_ASSERT_EQUAL_INT32(2437, bruceChannelToFreqMhz(6));
    TEST_ASSERT_EQUAL_INT32(2462, bruceChannelToFreqMhz(11));
    TEST_ASSERT_EQUAL_INT32(2484, bruceChannelToFreqMhz(14)); // special case
    TEST_ASSERT_EQUAL_INT32(5180, bruceChannelToFreqMhz(36));
    TEST_ASSERT_EQUAL_INT32(5745, bruceChannelToFreqMhz(149));
    TEST_ASSERT_EQUAL_INT32(5825, bruceChannelToFreqMhz(165));
    TEST_ASSERT_EQUAL_INT32(0, bruceChannelToFreqMhz(0));
    TEST_ASSERT_EQUAL_INT32(0, bruceChannelToFreqMhz(15));
}

void test_channel_in_band() {
    TEST_ASSERT_TRUE(bruceChannelInBand(6, BRUCE_BAND_24));
    TEST_ASSERT_FALSE(bruceChannelInBand(6, BRUCE_BAND_5));
    TEST_ASSERT_TRUE(bruceChannelInBand(149, BRUCE_BAND_5));
    TEST_ASSERT_FALSE(bruceChannelInBand(149, BRUCE_BAND_24));
}

void test_channel_plans_are_valid() {
    // 2.4 GHz plan: every entry is a real 2.4 GHz channel, 1/6/11 lead.
    TEST_ASSERT_TRUE(kBruceChannels24Count >= 3);
    TEST_ASSERT_EQUAL_UINT8(1, kBruceChannels24[0]);
    TEST_ASSERT_EQUAL_UINT8(6, kBruceChannels24[1]);
    TEST_ASSERT_EQUAL_UINT8(11, kBruceChannels24[2]);
    for (size_t i = 0; i < kBruceChannels24Count; ++i) {
        TEST_ASSERT_EQUAL_INT32(BRUCE_BAND_24, bruceBandForChannel(kBruceChannels24[i]));
    }

    // Dual-band plan: every entry classifies, and it actually contains 5 GHz.
    bool has5 = false, has24 = false;
    for (size_t i = 0; i < kBruceChannelsDualBandCount; ++i) {
        BruceWifiBand b = bruceBandForChannel(kBruceChannelsDualBand[i]);
        TEST_ASSERT_TRUE(b == BRUCE_BAND_24 || b == BRUCE_BAND_5);
        if (b == BRUCE_BAND_5) has5 = true;
        if (b == BRUCE_BAND_24) has24 = true;
    }
    TEST_ASSERT_TRUE(has5);
    TEST_ASSERT_TRUE(has24);
}

void test_active_plan_defaults_to_24_without_dualband() {
    // This TU is compiled without -DBRUCE_DUALBAND, so the selector returns 2.4.
    size_t count = 0;
    const uint8_t *plan = bruceScanChannelPlan(&count);
    TEST_ASSERT_EQUAL_PTR(kBruceChannels24, plan);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)kBruceChannels24Count, (uint32_t)count);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_band_for_channel);
    RUN_TEST(test_channel_to_freq);
    RUN_TEST(test_channel_in_band);
    RUN_TEST(test_channel_plans_are_valid);
    RUN_TEST(test_active_plan_defaults_to_24_without_dualband);
    return UNITY_END();
}
