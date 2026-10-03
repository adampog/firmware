// DIAL / cast network glue. See dial_cast.h. Drives the pure dial_protocol core
// over WiFiUDP (SSDP discovery, template: modules/wifi/responder.cpp) + HTTPClient
// (device description / app status / launch / stop, template:
// modules/bjs_interpreter/wifi_js.cpp). Requires an active STA connection.
#include "dial_cast.h"

#ifdef BRUCE_DIAL
#pragma message("Bruce DIAL/cast: ENABLED")

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <cstring>
#include <vector>

#include "core/display.h"          // drawMainBorderWithTitle, padprintln, displaySuccess/Error/...
#include "core/mykeyboard.h"       // check(), EscPress
#include "core/wifi/wifi_common.h" // wifiConnectMenu
#include "dial_protocol.h"
#include <globals.h> // returnToMenu, Option, loopOptions, MENU_TYPE_SUBMENU

#ifdef BOARD_HAS_PSRAM
static constexpr int DIAL_MAX_DEVICES = 16;
#else
static constexpr int DIAL_MAX_DEVICES = 6;
#endif
static constexpr uint16_t DIAL_LOCAL_PORT = 1901;     // M-SEARCH source port (replies return here)
static constexpr uint32_t DIAL_RECV_WINDOW_MS = 3000; // listen window per scan
static constexpr uint32_t DIAL_HTTP_TIMEOUT_MS = 5000;
static const char *const DIAL_DEFAULT_APP = "YouTube";

struct DialDevice {
    char location[256]; // UPnP device-description URL from the SSDP LOCATION header
    char appUrl[256];   // resolved DIAL Application-URL (REST base)
    char name[64];      // friendlyName from the device description
};

static DialDevice g_devices[DIAL_MAX_DEVICES];
static int g_deviceCount = 0;

int dialDeviceCount() { return g_deviceCount; }

static const char *dialStateStr(DialAppState s) {
    switch (s) {
        case DialAppState::Running: return "running";
        case DialAppState::Stopped: return "stopped";
        case DialAppState::Hidden: return "hidden";
        case DialAppState::Installable: return "installable";
        default: return "unknown";
    }
}

static bool dialHaveLocation(const char *loc) {
    for (int i = 0; i < g_deviceCount; ++i) {
        if (std::strcmp(g_devices[i].location, loc) == 0) return true;
    }
    return false;
}

// --- SSDP discovery (WiFiUDP multicast M-SEARCH, unicast replies) ---

static void dialDiscover(uint32_t windowMs) {
    g_deviceCount = 0;

    WiFiUDP udp;
    if (!udp.begin(DIAL_LOCAL_PORT)) {
        Serial.println("[DIAL] UDP begin failed");
        return;
    }

    char msearch[256];
    size_t mlen = dialBuildMSearch(msearch, sizeof(msearch), 2);
    const IPAddress mcast(239, 255, 255, 250);

    // A few M-SEARCH datagrams, like Ghost, to ride out loss.
    for (int i = 0; i < 3; ++i) {
        udp.beginPacket(mcast, DIAL_MULTICAST_PORT);
        udp.write(reinterpret_cast<const uint8_t *>(msearch), mlen);
        udp.endPacket();
        delay(100);
    }

    char buf[1024];
    const uint32_t start = millis();
    while (millis() - start < windowMs) {
        int n = udp.parsePacket();
        if (n <= 0) {
            delay(10);
            continue;
        }
        int len = udp.read(reinterpret_cast<uint8_t *>(buf), sizeof(buf) - 1);
        if (len <= 0) continue;
        buf[len] = '\0';
        if (!dialIsDialResponse(buf, (size_t)len)) continue;
        char loc[256];
        if (!dialParseHeader(buf, (size_t)len, "LOCATION", loc, sizeof(loc))) continue;
        if (dialHaveLocation(loc) || g_deviceCount >= DIAL_MAX_DEVICES) continue;
        DialDevice &d = g_devices[g_deviceCount];
        std::memset(&d, 0, sizeof(d));
        std::strncpy(d.location, loc, sizeof(d.location) - 1);
        ++g_deviceCount;
    }
    udp.stop();
}

// --- HTTP: resolve Application-URL + friendlyName from the device description ---

static bool dialResolveDevice(DialDevice &d) {
    if (d.location[0] == '\0') return false;
    if (d.appUrl[0] != '\0') return true; // already resolved

    HTTPClient http;
    http.setReuse(false);
    if (!http.begin(d.location)) return false;
    http.setTimeout(DIAL_HTTP_TIMEOUT_MS);
    http.collectAllHeaders(true);
    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }
    String appHeader = http.header("Application-URL"); // case-insensitive match
    String body = http.getString();
    http.end();

    if (appHeader.length() == 0) return false;
    char resolved[256];
    if (!dialResolveUrl(d.location, appHeader.c_str(), resolved, sizeof(resolved))) return false;
    std::strncpy(d.appUrl, resolved, sizeof(d.appUrl) - 1);

    char name[64];
    if (dialExtractTagText(body.c_str(), body.length(), "friendlyName", name, sizeof(name))) {
        std::strncpy(d.name, name, sizeof(d.name) - 1);
    } else {
        std::strncpy(d.name, "(unknown)", sizeof(d.name) - 1);
    }
    return true;
}

static DialAppState dialQueryApp(const DialDevice &d, const char *app) {
    if (d.appUrl[0] == '\0') return DialAppState::Unknown;
    char url[320];
    if (!dialBuildAppUrl(d.appUrl, app, url, sizeof(url))) return DialAppState::Unknown;
    HTTPClient http;
    http.setReuse(false);
    if (!http.begin(url)) return DialAppState::Unknown;
    http.setTimeout(DIAL_HTTP_TIMEOUT_MS);
    DialAppState st = DialAppState::Unknown;
    if (http.GET() == 200) {
        String body = http.getString();
        st = dialParseAppState(body.c_str(), body.length());
    }
    http.end();
    return st;
}

static bool dialLaunch(const DialDevice &d, const char *app, const char *videoId) {
    if (d.appUrl[0] == '\0') return false;
    char url[320];
    if (!dialBuildAppUrl(d.appUrl, app, url, sizeof(url))) return false;

    char body[128];
    body[0] = '\0';
    size_t bodyLen = 0;
    // YouTube honours an optional "v=<id>"; other apps launch bare. (Reliably
    // cueing a specific video on current YouTube TVs needs the Lounge protocol,
    // which is a separate follow-up; this opens the app and requests the video.)
    if (videoId && *videoId && String(app).equalsIgnoreCase("YouTube")) {
        bodyLen = dialBuildYouTubeLaunchBody(body, sizeof(body), videoId);
    }

    HTTPClient http;
    http.setReuse(false);
    if (!http.begin(url)) return false;
    http.setTimeout(DIAL_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Origin", "https://www.youtube.com");
    int code = http.sendRequest("POST", reinterpret_cast<uint8_t *>(body), bodyLen);
    http.end();
    return code == 200 || code == 201;
}

static bool dialStop(const DialDevice &d, const char *app) {
    if (d.appUrl[0] == '\0') return false;

    // DIAL stop = DELETE the app instance URL. Read the run href from the app
    // status (defaulting to "run" when absent).
    char href[64] = "run";
    char statusUrl[320];
    if (dialBuildAppUrl(d.appUrl, app, statusUrl, sizeof(statusUrl))) {
        HTTPClient httpg;
        httpg.setReuse(false);
        if (httpg.begin(statusUrl)) {
            httpg.setTimeout(DIAL_HTTP_TIMEOUT_MS);
            if (httpg.GET() == 200) {
                String body = httpg.getString();
                char h[64];
                if (dialExtractRunHref(body.c_str(), body.length(), h, sizeof(h))) {
                    std::strncpy(href, h, sizeof(href) - 1);
                    href[sizeof(href) - 1] = '\0';
                }
            }
            httpg.end();
        }
    }

    char stopUrl[400];
    if (!dialBuildStopUrl(d.appUrl, app, href, stopUrl, sizeof(stopUrl))) return false;
    HTTPClient http;
    http.setReuse(false);
    if (!http.begin(stopUrl)) return false;
    http.setTimeout(DIAL_HTTP_TIMEOUT_MS);
    int code = http.sendRequest("DELETE", static_cast<uint8_t *>(nullptr), 0);
    http.end();
    return code == 200 || code == 204;
}

// --- headless (serial) ---

void dialScanRun(uint32_t seconds) {
    if (!WiFi.isConnected()) wifiConnectMenu();
    if (!WiFi.isConnected()) {
        Serial.println("[DIAL] WiFi not connected");
        return;
    }
    uint32_t windowMs = seconds ? seconds * 1000 : DIAL_RECV_WINDOW_MS;
    if (windowMs > 30000) windowMs = 30000;

    Serial.println("[DIAL] discovering DIAL devices...");
    dialDiscover(windowMs);
    Serial.printf("[DIAL] %d device(s) found\n", g_deviceCount);

    for (int i = 0; i < g_deviceCount; ++i) {
        DialDevice &d = g_devices[i];
        bool ok = dialResolveDevice(d);
        DialAppState st = ok ? dialQueryApp(d, DIAL_DEFAULT_APP) : DialAppState::Unknown;
        Serial.printf(
            "[DIAL] %d: %s | %s | %s=%s\n", i, (d.name[0] ? d.name : "(unknown)"),
            (ok ? d.appUrl : d.location), DIAL_DEFAULT_APP, dialStateStr(st)
        );
    }
    if (g_deviceCount > 0) Serial.println("[DIAL] use: dial launch <idx> [app] [videoId] | dial stop <idx> [app]");
}

bool dialLaunchIndex(int index, const char *app, const char *videoId) {
    if (index < 0 || index >= g_deviceCount) return false;
    DialDevice &d = g_devices[index];
    if (!dialResolveDevice(d)) return false;
    if (!app || !*app) app = DIAL_DEFAULT_APP;
    bool ok = dialLaunch(d, app, videoId);
    Serial.printf("[DIAL] launch %s on %d: %s\n", app, index, ok ? "ok" : "failed");
    return ok;
}

bool dialStopIndex(int index, const char *app) {
    if (index < 0 || index >= g_deviceCount) return false;
    DialDevice &d = g_devices[index];
    if (!dialResolveDevice(d)) return false;
    if (!app || !*app) app = DIAL_DEFAULT_APP;
    bool ok = dialStop(d, app);
    Serial.printf("[DIAL] stop %s on %d: %s\n", app, index, ok ? "ok" : "failed");
    return ok;
}

// --- on-device UI ---

static void dialDeviceActionMenu(int idx) {
    if (idx < 0 || idx >= g_deviceCount) return;
    std::vector<Option> actions;
    actions.push_back(Option("Launch YouTube", [idx]() {
        bool ok = dialLaunchIndex(idx, "YouTube", nullptr);
        if (ok) displaySuccess("Launched YouTube", true);
        else displayError("Launch failed", true);
    }));
    actions.push_back(Option("Stop YouTube", [idx]() {
        bool ok = dialStopIndex(idx, "YouTube");
        if (ok) displaySuccess("Stopped YouTube", true);
        else displayError("Stop failed", true);
    }));
    actions.push_back(Option("YouTube status", [idx]() {
        DialAppState st = dialQueryApp(g_devices[idx], "YouTube");
        displayInfo(String("YouTube: ") + dialStateStr(st), true);
    }));
    actions.push_back(Option("Back", []() {}));
    returnToMenu = false;
    loopOptions(actions, MENU_TYPE_SUBMENU, g_devices[idx].name);
}

void dialCastScreen() {
    if (!WiFi.isConnected()) wifiConnectMenu();
    if (!WiFi.isConnected()) {
        displayError("WiFi not connected", true);
        return;
    }

    drawMainBorderWithTitle("DIAL / Cast");
    padprintln("");
    padprintln("Discovering DIAL devices...");
    dialDiscover(DIAL_RECV_WINDOW_MS);

    if (g_deviceCount == 0) {
        displayWarning("No DIAL devices found", true);
        return;
    }
    for (int i = 0; i < g_deviceCount; ++i) dialResolveDevice(g_devices[i]);

    std::vector<Option> devs;
    for (int i = 0; i < g_deviceCount; ++i) {
        String label = String(i) + ": " + (g_devices[i].name[0] ? String(g_devices[i].name) : String("(unknown)"));
        int idx = i;
        devs.push_back(Option(label.c_str(), [idx]() { dialDeviceActionMenu(idx); }));
    }
    devs.push_back(Option("Back", []() {}));
    returnToMenu = false;
    loopOptions(devs, MENU_TYPE_SUBMENU, "DIAL devices");
}

#endif // BRUCE_DIAL
