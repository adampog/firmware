// Minimal Unity-compatible shim so the DIAL protocol-core unit tests (which
// #include <unity.h>) build and run with a plain host g++ — no PlatformIO native
// env or Unity dependency required. Same shim pattern as test/remoteid,
// test/pinescan, test/foxhunt. Each test binary is a single TU that includes this
// header; the pure dial_protocol.cpp is compiled alongside.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

// Provided by each test file.
void setUp();
void tearDown();

static int ut_tests = 0;
static int ut_failures = 0;
static int ut_current_failed = 0;

#define UNITY_BEGIN()                                                                                       \
    do {                                                                                                     \
        ut_tests = 0;                                                                                        \
        ut_failures = 0;                                                                                     \
    } while (0)

#define UNITY_END() (std::printf("\n%d Tests %d Failures\n", ut_tests, ut_failures), ut_failures)

#define RUN_TEST(fn)                                                                                        \
    do {                                                                                                     \
        ut_current_failed = 0;                                                                               \
        ++ut_tests;                                                                                          \
        setUp();                                                                                             \
        fn();                                                                                                \
        tearDown();                                                                                          \
        std::printf("%-56s %s\n", #fn, ut_current_failed ? "FAIL" : "PASS");                                 \
    } while (0)

#define UT_FAIL_(msg)                                                                                       \
    do {                                                                                                     \
        std::printf("  ASSERT FAIL (%s:%d): %s\n", __FILE__, __LINE__, msg);                                 \
        if (!ut_current_failed) ++ut_failures;                                                               \
        ut_current_failed = 1;                                                                               \
        return;                                                                                              \
    } while (0)

#define TEST_ASSERT_TRUE(c)                                                                                 \
    do {                                                                                                     \
        if (!(c)) UT_FAIL_("expected true: " #c);                                                            \
    } while (0)
#define TEST_ASSERT_FALSE(c)                                                                                \
    do {                                                                                                     \
        if (c) UT_FAIL_("expected false: " #c);                                                              \
    } while (0)
#define TEST_ASSERT_NULL(p)                                                                                 \
    do {                                                                                                     \
        if ((p) != nullptr) UT_FAIL_("expected NULL: " #p);                                                  \
    } while (0)
#define TEST_ASSERT_NOT_NULL(p)                                                                             \
    do {                                                                                                     \
        if ((p) == nullptr) UT_FAIL_("expected non-NULL: " #p);                                              \
    } while (0)

#define TEST_ASSERT_EQUAL_STRING(e, a)                                                                      \
    do {                                                                                                     \
        if (std::strcmp((e), (a)) != 0) {                                                                     \
            std::printf("  got \"%s\" expected \"%s\"\n", (a), (e));                                          \
            UT_FAIL_("string mismatch");                                                                     \
        }                                                                                                     \
    } while (0)

#define UT_EQ_(e, a, nm)                                                                                    \
    do {                                                                                                     \
        long long ut_e = (long long)(e), ut_a = (long long)(a);                                              \
        if (ut_e != ut_a) {                                                                                   \
            std::printf("  got %lld expected %lld\n", ut_a, ut_e);                                            \
            UT_FAIL_(nm " mismatch");                                                                        \
        }                                                                                                     \
    } while (0)
#define TEST_ASSERT_EQUAL_INT(e, a) UT_EQ_(e, a, "int")
#define TEST_ASSERT_EQUAL_UINT(e, a) UT_EQ_(e, a, "uint")
#define TEST_ASSERT_EQUAL_UINT8(e, a) UT_EQ_(e, a, "uint8")
#define TEST_ASSERT_EQUAL_UINT32(e, a) UT_EQ_(e, a, "uint32")
#define TEST_ASSERT_EQUAL_size_t(e, a) UT_EQ_(e, a, "size_t")
