// Host unit tests for the pure DIAL/cast protocol core (src/modules/dial).
// Built + run with plain g++ via .github/workflows/dial_native_test.yml, using
// the same unity.h shim pattern as the remoteid/pinescan/foxhunt tests.
#include <unity.h>

#include <cstring>

#include "dial_protocol.h"

void setUp() {}
void tearDown() {}

// --- dialBuildMSearch -------------------------------------------------------

void test_msearch_has_required_fields() {
    char buf[256];
    size_t n = dialBuildMSearch(buf, sizeof(buf), 2);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(std::strlen(buf), n);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "M-SEARCH * HTTP/1.1\r\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "HOST: 239.255.255.250:1900\r\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "MAN: \"ssdp:discover\"\r\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "ST: urn:dial-multiscreen-org:service:dial:1\r\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "MX: 2\r\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "\r\n\r\n")); // blank terminating line
}

void test_msearch_clamps_mx_and_rejects_small_buffer() {
    char buf[256];
    dialBuildMSearch(buf, sizeof(buf), 0);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "MX: 1\r\n")); // 0 -> 1
    dialBuildMSearch(buf, sizeof(buf), 99);
    TEST_ASSERT_NOT_NULL(std::strstr(buf, "MX: 5\r\n")); // clamped to 5
    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, dialBuildMSearch(tiny, sizeof(tiny), 2)); // truncation -> 0
}

// --- dialParseHeader --------------------------------------------------------

static const char kSsdp[] =
    "HTTP/1.1 200 OK\r\n"
    "CACHE-CONTROL: max-age=1800\r\n"
    "LOCATION: http://192.168.1.50:8008/ssdp/device-desc.xml\r\n"
    "ST: urn:dial-multiscreen-org:service:dial:1\r\n"
    "USN: uuid:abcd::urn:dial-multiscreen-org:service:dial:1\r\n"
    "\r\n";

void test_parse_header_case_insensitive_and_trimmed() {
    char v[128];
    TEST_ASSERT_TRUE(dialParseHeader(kSsdp, std::strlen(kSsdp), "location", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:8008/ssdp/device-desc.xml", v);
    // Different case name still matches; value whitespace trimmed.
    TEST_ASSERT_TRUE(dialParseHeader(kSsdp, std::strlen(kSsdp), "ST", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("urn:dial-multiscreen-org:service:dial:1", v);
}

void test_parse_header_missing_and_lf_only() {
    char v[128];
    TEST_ASSERT_FALSE(dialParseHeader(kSsdp, std::strlen(kSsdp), "Application-URL", v, sizeof(v)));
    const char *lfHeaders = "HTTP/1.1 200 OK\nApplication-URL: http://host:8008/apps\n\n";
    TEST_ASSERT_TRUE(dialParseHeader(lfHeaders, std::strlen(lfHeaders), "Application-URL", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("http://host:8008/apps", v);
}

void test_parse_header_does_not_match_substring_name() {
    // "ST" must not be matched by the "USN"/other lines; exact header-name match.
    char v[128];
    const char *h = "X-ST-Extra: nope\r\nST: yes\r\n\r\n";
    TEST_ASSERT_TRUE(dialParseHeader(h, std::strlen(h), "ST", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("yes", v);
}

// --- dialIsDialResponse -----------------------------------------------------

void test_is_dial_response() {
    TEST_ASSERT_TRUE(dialIsDialResponse(kSsdp, std::strlen(kSsdp)));
    const char *other = "HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";
    TEST_ASSERT_FALSE(dialIsDialResponse(other, std::strlen(other)));
}

// --- dialExtractTagText -----------------------------------------------------

static const char kDeviceDesc[] =
    R"(<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <device>
    <deviceType>urn:dial-multiscreen-org:device:dial:1</deviceType>
    <friendlyName>Living Room TV</friendlyName>
    <manufacturer>Acme</manufacturer>
  </device>
</root>)";

void test_extract_tag_text() {
    char v[64];
    TEST_ASSERT_TRUE(dialExtractTagText(kDeviceDesc, std::strlen(kDeviceDesc), "friendlyName", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("Living Room TV", v);
    TEST_ASSERT_TRUE(dialExtractTagText(kDeviceDesc, std::strlen(kDeviceDesc), "manufacturer", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("Acme", v);
    // Missing tag -> false.
    TEST_ASSERT_FALSE(dialExtractTagText(kDeviceDesc, std::strlen(kDeviceDesc), "modelName", v, sizeof(v)));
}

void test_extract_tag_boundary_and_self_closing() {
    char v[64];
    // "name" must not match "<friendlyName>".
    const char *x = "<friendlyName>FN</friendlyName><name>NM</name>";
    TEST_ASSERT_TRUE(dialExtractTagText(x, std::strlen(x), "name", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("NM", v);
    // Self-closing tag has no text -> skip it, fall through to false.
    const char *sc = "<state/>";
    TEST_ASSERT_FALSE(dialExtractTagText(sc, std::strlen(sc), "state", v, sizeof(v)));
}

// --- dialParseAppState ------------------------------------------------------

void test_parse_app_state() {
    const char *running = "<service><name>YouTube</name><state>running</state></service>";
    const char *stopped = "<service><name>YouTube</name><state>stopped</state></service>";
    const char *hidden = "<service><state>hidden</state></service>";
    const char *inst = R"(<service><state>installable=http://host/apps/YouTube</state></service>)";
    TEST_ASSERT_TRUE(dialParseAppState(running, std::strlen(running)) == DialAppState::Running);
    TEST_ASSERT_TRUE(dialParseAppState(stopped, std::strlen(stopped)) == DialAppState::Stopped);
    TEST_ASSERT_TRUE(dialParseAppState(hidden, std::strlen(hidden)) == DialAppState::Hidden);
    TEST_ASSERT_TRUE(dialParseAppState(inst, std::strlen(inst)) == DialAppState::Installable);
    TEST_ASSERT_TRUE(dialParseAppState("<x/>", 4) == DialAppState::Unknown);
}

// --- dialBuildAppUrl --------------------------------------------------------

void test_build_app_url() {
    char v[128];
    TEST_ASSERT_TRUE(dialBuildAppUrl("http://host:8008/apps", "YouTube", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("http://host:8008/apps/YouTube", v);
    // Trailing slash on base + leading slash on app collapse to one.
    TEST_ASSERT_TRUE(dialBuildAppUrl("http://host:8008/apps/", "/YouTube", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("http://host:8008/apps/YouTube", v);
    TEST_ASSERT_FALSE(dialBuildAppUrl("", "YouTube", v, sizeof(v)));
    TEST_ASSERT_FALSE(dialBuildAppUrl("http://host/apps", "", v, sizeof(v)));
}

// --- dialExtractRunHref / dialBuildStopUrl ----------------------------------

void test_run_href_and_stop_url() {
    char href[32];
    const char *status =
        R"(<service><name>YouTube</name><options allowStop="true"/><state>running</state><link rel="run" href="run"/></service>)";
    TEST_ASSERT_TRUE(dialExtractRunHref(status, std::strlen(status), href, sizeof(href)));
    TEST_ASSERT_EQUAL_STRING("run", href);

    char stop[160];
    TEST_ASSERT_TRUE(dialBuildStopUrl("http://host:8008/apps", "YouTube", href, stop, sizeof(stop)));
    TEST_ASSERT_EQUAL_STRING("http://host:8008/apps/YouTube/run", stop);
    // NULL href defaults to "run".
    TEST_ASSERT_TRUE(dialBuildStopUrl("http://host:8008/apps", "YouTube", nullptr, stop, sizeof(stop)));
    TEST_ASSERT_EQUAL_STRING("http://host:8008/apps/YouTube/run", stop);
    // No <link rel="run"> present -> false (caller defaults to "run").
    const char *noLink = "<service><state>stopped</state></service>";
    TEST_ASSERT_FALSE(dialExtractRunHref(noLink, std::strlen(noLink), href, sizeof(href)));
}

// --- dialResolveUrl ---------------------------------------------------------

void test_resolve_url() {
    char v[160];
    // Absolute passes through.
    TEST_ASSERT_TRUE(dialResolveUrl("http://host:8008/dd.xml", "http://host:8008/apps", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("http://host:8008/apps", v);
    // Relative resolved against scheme://authority of the base.
    TEST_ASSERT_TRUE(dialResolveUrl("http://192.168.1.50:8008/ssdp/device-desc.xml", "/apps", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:8008/apps", v);
    TEST_ASSERT_TRUE(dialResolveUrl("http://192.168.1.50:8008/ssdp/dd.xml", "apps", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50:8008/apps", v);
}

// --- dialBuildYouTubeLaunchBody ---------------------------------------------

void test_youtube_launch_body() {
    char v[64];
    size_t n = dialBuildYouTubeLaunchBody(v, sizeof(v), "dQw4w9WgXcQ");
    TEST_ASSERT_EQUAL_STRING("v=dQw4w9WgXcQ", v);
    TEST_ASSERT_EQUAL_size_t(std::strlen(v), n);
    // Empty/NULL video id -> empty body.
    TEST_ASSERT_EQUAL_size_t(0, dialBuildYouTubeLaunchBody(v, sizeof(v), ""));
    TEST_ASSERT_EQUAL_STRING("", v);
    TEST_ASSERT_EQUAL_size_t(0, dialBuildYouTubeLaunchBody(v, sizeof(v), nullptr));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_msearch_has_required_fields);
    RUN_TEST(test_msearch_clamps_mx_and_rejects_small_buffer);
    RUN_TEST(test_parse_header_case_insensitive_and_trimmed);
    RUN_TEST(test_parse_header_missing_and_lf_only);
    RUN_TEST(test_parse_header_does_not_match_substring_name);
    RUN_TEST(test_is_dial_response);
    RUN_TEST(test_extract_tag_text);
    RUN_TEST(test_extract_tag_boundary_and_self_closing);
    RUN_TEST(test_parse_app_state);
    RUN_TEST(test_build_app_url);
    RUN_TEST(test_run_href_and_stop_url);
    RUN_TEST(test_resolve_url);
    RUN_TEST(test_youtube_launch_body);
    return UNITY_END();
}
