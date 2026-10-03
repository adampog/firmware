// WiFi Pineapple / Evil-Twin detector capture. See pine_scan.h.
// Promiscuous bring-up/teardown is the shared wifi_common passive helper (also
// used by Remote-ID and fox-hunt); classification is the pure core in
// pinescan_detector.{h,cpp}.
#include "pine_scan.h"

#ifdef BRUCE_PINESCAN

#include <Arduino.h>

#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

#include "core/display.h"          // tft, drawMainBorderWithTitle, bruceConfig, tftWidth/Height
#include "core/mykeyboard.h"       // check(), EscPress
#include "core/net_utils.h"        // macToString
#include "core/wifi/wifi_common.h" // wifiStart/StopPassivePromiscuous
#include "modules/wifi/band_plan.h" // shared 2.4 / dual-band hop plan
#include "pinescan_detector.h"
#include <globals.h> // returnToMenu

#ifdef BOARD_HAS_PSRAM
static constexpr size_t PINESCAN_CAPACITY = 48;
#else
static constexpr size_t PINESCAN_CAPACITY = 16;
#endif
static constexpr uint32_t PINESCAN_SCAN_DEFAULT_MS = 30000;
static constexpr uint32_t PINESCAN_HOP_MS = 300;

// Shared hop plan from band_plan: 2.4 GHz normally, 2.4+5 GHz on the ESP32-C5
// (-DBRUCE_DUALBAND). The 2.4 plan is the former local {1,6,11,...} list.
#ifdef BRUCE_DUALBAND
static const uint8_t *const kChannels = kBruceChannelsDualBand;
static const size_t kChannelCount = kBruceChannelsDualBandCount;
#else
static const uint8_t *const kChannels = kBruceChannels24;
static const size_t kChannelCount = kBruceChannels24Count;
#endif

struct PineScanHit {
    uint8_t mac[6];
    char essid[33];
    const char *vendor; // points into the static kPineScanVendors table or a literal
    char type[12];      // "SUSP_OUI" | "TAG+CAP"
    uint8_t channel;
    int8_t rssi;
};

static PineScanHit g_hits[PINESCAN_CAPACITY];
static size_t g_count = 0;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

static void extractSsid(const uint8_t *frame, size_t len, char *out, size_t outSize) {
    out[0] = '\0';
    if (len < 38) return;
    size_t ssidLen = frame[37];
    if (ssidLen == 0 || 38 + ssidLen > len) return;
    if (ssidLen > outSize - 1) ssidLen = outSize - 1;
    size_t n = 0;
    for (size_t i = 0; i < ssidLen; ++i) {
        const uint8_t c = frame[38 + i];
        out[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.'; // printable only
    }
    out[n] = '\0';
}

// Insert or refresh a suspicious AP in the store (called from the RX callback).
static void recordHit(const uint8_t mac[6], const PineScanVerdict &v, const char *essid, int8_t rssi) {
    portENTER_CRITICAL(&g_mux);
    PineScanHit *hit = nullptr;
    for (size_t i = 0; i < g_count; ++i) {
        if (std::memcmp(g_hits[i].mac, mac, 6) == 0) {
            hit = &g_hits[i];
            break;
        }
    }
    if (hit == nullptr) {
        if (g_count >= PINESCAN_CAPACITY) {
            portEXIT_CRITICAL(&g_mux);
            return; // store full; keep earliest detections
        }
        hit = &g_hits[g_count++];
        std::memcpy(hit->mac, mac, 6);
    }
    std::strncpy(hit->essid, essid, sizeof(hit->essid) - 1);
    hit->essid[sizeof(hit->essid) - 1] = '\0';
    hit->vendor = v.vendorName;
    std::strncpy(hit->type, v.suspiciousOui ? "SUSP_OUI" : "TAG+CAP", sizeof(hit->type) - 1);
    hit->type[sizeof(hit->type) - 1] = '\0';
    hit->rssi = rssi;
    hit->channel = (v.channel > 0) ? (uint8_t)v.channel : 0;
    portEXIT_CRITICAL(&g_mux);
}

static void pineScanWifiCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || buf == nullptr) return;
    wifi_promiscuous_pkt_t *packet = static_cast<wifi_promiscuous_pkt_t *>(buf);
    size_t len = packet->rx_ctrl.sig_len;
    if (len >= 4) len -= 4; // strip FCS
    if (len < 38 || packet->payload[0] != 0x80) return; // beacons only

    PineScanVerdict v = pineScanClassify(packet->payload, len);
    if (!v.suspicious) return;

    char essid[33];
    extractSsid(packet->payload, len, essid, sizeof(essid));
    uint8_t mac[6];
    std::memcpy(mac, packet->payload + 10, 6); // SA
    if (v.channel <= 0) v.channel = packet->rx_ctrl.channel;
    recordHit(mac, v, essid, packet->rx_ctrl.rssi);
}

size_t pineScanHitCount() {
    portENTER_CRITICAL(&g_mux);
    const size_t n = g_count;
    portEXIT_CRITICAL(&g_mux);
    return n;
}

static bool copyHit(size_t i, PineScanHit &out) {
    bool ok = false;
    portENTER_CRITICAL(&g_mux);
    if (i < g_count) {
        out = g_hits[i];
        ok = true;
    }
    portEXIT_CRITICAL(&g_mux);
    return ok;
}

// --- shared radio lifecycle (WiFi-only passive promiscuous; mirrors sniffer.cpp) ---

static void pineScanReset() {
    portENTER_CRITICAL(&g_mux);
    g_count = 0;
    portEXIT_CRITICAL(&g_mux);
}

static void pineScanRadioStart() { wifiStartPassivePromiscuous(pineScanWifiCallback, kChannels[0]); }

static void pineScanRadioStop() { wifiStopPassivePromiscuous(); }

static void dumpHit(const PineScanHit &h) {
    String line = "PINE," + macToString(h.mac) + ",ch=" + String(h.channel) + ",rssi=" + String(h.rssi) +
                  ",[" + String(h.type) + "]," + String(h.vendor) + ",ssid=" + String(h.essid);
    Serial.println(line);
}

void pineScanRun(uint32_t durationMs) {
    if (durationMs == 0) durationMs = PINESCAN_SCAN_DEFAULT_MS;
    pineScanReset();
    pineScanRadioStart();
    Serial.printf("[PINE] scan started (%us); press any key to stop\n", durationMs / 1000);

    const uint32_t start = millis();
    uint32_t lastHop = start, lastTick = start;
    size_t chIdx = 0;
    while (millis() - start < durationMs) {
        const uint32_t now = millis();
        if (now - lastHop >= PINESCAN_HOP_MS) {
            chIdx = (chIdx + 1) % kChannelCount;
            esp_wifi_set_channel(kChannels[chIdx], WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }
        if (now - lastTick >= 1000) {
            Serial.printf("[PINE] ch=%u suspicious=%u\n", kChannels[chIdx], (unsigned)pineScanHitCount());
            lastTick = now;
        }
        if (Serial.available()) {
            while (Serial.available()) Serial.read();
            Serial.println("[PINE] stopped by key");
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    pineScanRadioStop();

    const size_t n = pineScanHitCount();
    Serial.printf("[PINE] scan done, %u suspicious AP(s)\n", (unsigned)n);
    for (size_t i = 0; i < n; ++i) {
        PineScanHit h;
        if (copyHit(i, h)) dumpHit(h);
    }
}

static void pineScanRender(uint8_t channel) {
    tft.fillScreen(bruceConfig.bgColor);
    drawMainBorderWithTitle("Pineapple Detect");
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    const int16_t x = 8;
    const int16_t lineH = LH * FP + 2;
    int16_t y = BORDER_PAD_Y + FM * LH;

    const size_t n = pineScanHitCount();
    tft.setCursor(x, y);
    tft.print("Rogue APs:" + String((unsigned)n) + "  CH:" + String(channel));
    y += lineH + 2;

    const int16_t bottom = tftHeight - lineH - 4;
    for (size_t i = 0; i < n && y < bottom; ++i) {
        PineScanHit h;
        if (!copyHit(i, h)) continue;
        String id = h.essid[0] ? String(h.essid) : macToString(h.mac);
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.setCursor(x, y);
        tft.print(String(h.rssi) + " c" + String(h.channel) + " " + id);
        y += lineH;
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(x, tftHeight - lineH - 2);
    tft.print("ESC: exit");
}

void pineScanScreen() {
    returnToMenu = false;
    pineScanReset();
    pineScanRadioStart();
    tft.fillScreen(bruceConfig.bgColor);

    size_t chIdx = 0;
    uint32_t lastHop = millis(), lastRender = 0;
    for (;;) {
        if (returnToMenu) break;
        if (check(EscPress)) {
            returnToMenu = true;
            break;
        }
        const uint32_t now = millis();
        if (now - lastHop >= PINESCAN_HOP_MS) {
            chIdx = (chIdx + 1) % kChannelCount;
            esp_wifi_set_channel(kChannels[chIdx], WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }
        if (now - lastRender >= 500) {
            pineScanRender(kChannels[chIdx]);
            lastRender = now;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    pineScanRadioStop();
}

#endif // BRUCE_PINESCAN
