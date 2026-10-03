// WiFi fox-hunt / direction-finding capture. See fox_hunt.h.
// Promiscuous capture + WiFi init/teardown mirror src/modules/pinescan/pine_scan.cpp;
// the matching + RSSI smoothing is the pure core in fox_hunt_detector.{h,cpp}.
#include "fox_hunt.h"

#ifdef BRUCE_FOXHUNT

#include <Arduino.h>
#include <WiFi.h>

#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <cstring>

#include "core/display.h"          // tft, drawMainBorderWithTitle, bruceConfig, tftWidth/Height
#include "core/mykeyboard.h"       // check(), EscPress
#include "core/net_utils.h"        // macToString
#include "core/wifi/wifi_common.h" // ensureWifiPlatform, wifiDisconnect
#include "fox_hunt_detector.h"
#include <globals.h> // returnToMenu, Option, loopOptions

static constexpr uint32_t FOX_SCAN_DEFAULT_MS = 60000;
static constexpr uint32_t FOX_HOP_MS = 250;       // acquisition hop interval
static constexpr uint32_t FOX_STALE_MS = 4000;    // target considered lost after this
static constexpr uint32_t FOX_UI_MS = 500;        // report / render cadence
static constexpr int FOX_ALPHA = 40;              // RSSI EMA smoothing (%)
static constexpr int FOX_TREND_DEADBAND = 2;      // dB guard band for warmer/colder
static constexpr int FOX_RSSI_FLOOR = -95;        // 0 bars
static constexpr int FOX_RSSI_CEIL = -30;         // full bars
static constexpr uint8_t FOX_MAX_24_CHANNEL = 13; // 2.4GHz round-robin upper bound

// Next channel to tune while acquiring a target of unknown channel. In the
// dual-band build (#4) this walks the 5GHz plan too; here it is the 2.4GHz
// 1..13 round-robin from the ported foxHuntNextChannel helper.
static uint8_t nextAcquireChannel(uint8_t current) {
    return foxHuntNextChannel(current, FOX_MAX_24_CHANNEL);
}

// --- shared hunt state (written from the promiscuous RX callback) ---
static uint8_t g_target[6] = {0};
static bool g_hasSample = false;
static int g_rssiEma = FOX_RSSI_FLOOR;
static int8_t g_lastRaw = 0;
static uint8_t g_observedChannel = 0; // channel a target frame was last heard on
static uint32_t g_lastSeenMs = 0;
static uint32_t g_matchCount = 0;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

static void foxHuntReset() {
    portENTER_CRITICAL(&g_mux);
    g_hasSample = false;
    g_rssiEma = FOX_RSSI_FLOOR;
    g_lastRaw = 0;
    g_observedChannel = 0;
    g_lastSeenMs = 0;
    g_matchCount = 0;
    portEXIT_CRITICAL(&g_mux);
}

static void foxHuntWifiCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (buf == nullptr || (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA)) return;
    wifi_promiscuous_pkt_t *packet = static_cast<wifi_promiscuous_pkt_t *>(buf);
    size_t len = packet->rx_ctrl.sig_len;
    if (len >= 4) len -= 4; // strip FCS
    if (len < 22) return;   // need addr1/addr2/addr3
    if (!foxHuntFrameMatchesTarget(packet->payload, len, g_target)) return;

    const int8_t rssi = packet->rx_ctrl.rssi;
    const uint8_t channel = packet->rx_ctrl.channel;
    const uint32_t now = millis();

    portENTER_CRITICAL(&g_mux);
    if (!g_hasSample) {
        g_rssiEma = rssi;
        g_hasSample = true;
    } else {
        g_rssiEma = foxHuntSmoothRssi(g_rssiEma, rssi, FOX_ALPHA);
    }
    g_lastRaw = rssi;
    if (foxHuntShouldUpdateChannel(false, channel)) g_observedChannel = channel;
    g_lastSeenMs = now;
    ++g_matchCount;
    portEXIT_CRITICAL(&g_mux);
}

struct FoxSnapshot {
    bool hasSample;
    int rssiEma;
    int8_t lastRaw;
    uint8_t channel;
    uint32_t lastSeenMs;
    uint32_t matchCount;
};

static FoxSnapshot foxHuntSnapshot() {
    FoxSnapshot s;
    portENTER_CRITICAL(&g_mux);
    s.hasSample = g_hasSample;
    s.rssiEma = g_rssiEma;
    s.lastRaw = g_lastRaw;
    s.channel = g_observedChannel;
    s.lastSeenMs = g_lastSeenMs;
    s.matchCount = g_matchCount;
    portEXIT_CRITICAL(&g_mux);
    return s;
}

// --- shared radio lifecycle (WiFi-only passive promiscuous; mirrors pine_scan.cpp) ---

static void foxHuntRadioStart(uint8_t startChannel) {
    ensureWifiPlatform();
    nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL)); // passive
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(foxHuntWifiCallback);
    if (startChannel == 0) startChannel = 1;
    esp_wifi_set_channel(startChannel, WIFI_SECOND_CHAN_NONE);
}

static void foxHuntRadioStop() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_set_promiscuous_rx_cb(NULL);
    wifiDisconnect();
}

// --- headless (serial) ---

void foxHuntRun(const uint8_t target[6], uint32_t durationMs) {
    if (durationMs == 0) durationMs = FOX_SCAN_DEFAULT_MS;
    std::memcpy(g_target, target, 6);
    foxHuntReset();
    foxHuntRadioStart(1);
    Serial.printf(
        "[FOX] hunting %s (%us); press any key to stop\n", macToString(g_target).c_str(), durationMs / 1000
    );

    uint8_t campChannel = 0; // unknown -> acquire by hopping
    uint8_t acquireChannel = 1;
    const uint32_t start = millis();
    uint32_t lastHop = start, lastTick = start;
    int prevRssi = FOX_RSSI_FLOOR;
    bool havePrev = false;

    while (millis() - start < durationMs) {
        const uint32_t now = millis();
        if (Serial.available()) {
            while (Serial.available()) Serial.read();
            Serial.println("[FOX] stopped by key");
            break;
        }
        FoxSnapshot s = foxHuntSnapshot();
        if (campChannel == 0 && s.channel > 0) {
            campChannel = s.channel;
            esp_wifi_set_channel(campChannel, WIFI_SECOND_CHAN_NONE);
        } else if (campChannel == 0 && now - lastHop >= FOX_HOP_MS) {
            acquireChannel = nextAcquireChannel(acquireChannel);
            esp_wifi_set_channel(acquireChannel, WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }
        if (now - lastTick >= FOX_UI_MS) {
            const uint8_t ch = campChannel ? campChannel : acquireChannel;
            if (!s.hasSample) {
                Serial.printf("[FOX] ch=%u acquiring...\n", ch);
            } else {
                const bool lost = foxHuntTargetIsStale(now, s.lastSeenMs, FOX_STALE_MS);
                const int bars = foxHuntMeterLevel(s.rssiEma, FOX_RSSI_FLOOR, FOX_RSSI_CEIL, 10);
                const char *trend = "= (steady)";
                if (havePrev) {
                    switch (foxHuntTrend(prevRssi, s.rssiEma, FOX_TREND_DEADBAND)) {
                        case FOX_WARMER: trend = "+ (warmer)"; break;
                        case FOX_COLDER: trend = "- (colder)"; break;
                        default: trend = "= (steady)"; break;
                    }
                }
                Serial.printf(
                    "[FOX] ch=%u rssi=%ddBm avg=%d bars=%d/10 %s%s\n", ch, s.lastRaw, s.rssiEma, bars, trend,
                    lost ? " [LOST]" : ""
                );
                prevRssi = s.rssiEma;
                havePrev = true;
            }
            lastTick = now;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    const uint32_t frames = foxHuntSnapshot().matchCount;
    foxHuntRadioStop();
    Serial.printf("[FOX] done (%u frames from target)\n", (unsigned)frames);
}

// --- on-device target picker + homing screen ---

struct FoxTarget {
    uint8_t mac[6];
    uint8_t channel;
    char label[33];
};

// Quick scan + list; returns false if cancelled or nothing found.
static bool foxHuntPickTarget(FoxTarget &out) {
    displayTextLine("Scanning..");
    WiFi.mode(WIFI_STA);
    const int nets = WiFi.scanNetworks(false, true);
    if (nets <= 0) {
        wifiDisconnect();
        displayError("No networks found", true);
        return false;
    }

    int chosen = -1;
    std::vector<Option> opts;
    for (int i = 0; i < nets; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) ssid = "<hidden> " + WiFi.BSSIDstr(i);
        String label = ssid + " (" + String(WiFi.RSSI(i)) + "|ch" + String(WiFi.channel(i)) + ")";
        opts.push_back(Option(label, [&chosen, i]() { chosen = i; }));
    }
    opts.push_back(Option("Cancel", [&chosen]() { chosen = -1; }));

    returnToMenu = false;
    loopOptions(opts);

    bool ok = false;
    if (chosen >= 0 && chosen < nets) {
        std::memcpy(out.mac, WiFi.BSSID(chosen), 6);
        out.channel = (uint8_t)WiFi.channel(chosen);
        String ssid = WiFi.SSID(chosen);
        if (ssid.length() == 0) ssid = WiFi.BSSIDstr(chosen);
        std::strncpy(out.label, ssid.c_str(), sizeof(out.label) - 1);
        out.label[sizeof(out.label) - 1] = '\0';
        ok = true;
    }

    WiFi.scanDelete();
    wifiDisconnect(); // full deinit so foxHuntRadioStart() can re-init cleanly
    return ok;
}

static void foxHuntRender(
    const FoxTarget &target, const FoxSnapshot &s, uint8_t channel, int prevRssi, bool havePrev
) {
    tft.fillScreen(bruceConfig.bgColor);
    drawMainBorderWithTitle("Fox Hunt");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    const int16_t x = 8;
    int16_t y = BORDER_PAD_Y + FM * LH;

    tft.setTextSize(FP);
    tft.setCursor(x, y);
    tft.print(String(target.label));
    y += LH * FP + 2;
    tft.setCursor(x, y);
    tft.print("ch" + String(channel) + "  " + macToString(target.mac));
    y += LH * FP + 6;

    const bool lost = s.hasSample && foxHuntTargetIsStale(millis(), s.lastSeenMs, FOX_STALE_MS);
    uint16_t color = bruceConfig.priColor;
    const char *trend = "--";
    if (!s.hasSample) {
        trend = "acquiring";
    } else if (havePrev) {
        switch (foxHuntTrend(prevRssi, s.rssiEma, FOX_TREND_DEADBAND)) {
            case FOX_WARMER:
                color = TFT_GREEN;
                trend = "WARMER ^";
                break;
            case FOX_COLDER:
                color = TFT_RED;
                trend = "colder v";
                break;
            default: trend = "steady =="; break;
        }
    }
    if (lost) {
        color = TFT_RED;
        trend = "LOST";
    }

    // Big RSSI number.
    tft.setTextSize(FG);
    tft.setTextColor(color, bruceConfig.bgColor);
    tft.setCursor(x, y);
    tft.print(s.hasSample ? (String(s.rssiEma) + "dBm") : String("--"));
    y += LH * FG + 4;

    // Proximity bar.
    const int16_t barX = x, barW = tftWidth - 2 * x, barH = 12;
    tft.drawRect(barX, y, barW, barH, bruceConfig.priColor);
    const int level =
        s.hasSample ? foxHuntMeterLevel(s.rssiEma, FOX_RSSI_FLOOR, FOX_RSSI_CEIL, barW - 2) : 0;
    if (level > 0) tft.fillRect(barX + 1, y + 1, level, barH - 2, color);
    y += barH + 6;

    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(x, y);
    tft.print(String(trend) + "  pkts:" + String((unsigned)s.matchCount));

    tft.setCursor(x, tftHeight - LH * FP - 2);
    tft.print("ESC: exit");
}

void foxHuntScreen() {
    returnToMenu = false;
    FoxTarget target;
    std::memset(&target, 0, sizeof(target));
    if (!foxHuntPickTarget(target)) {
        returnToMenu = true;
        return;
    }

    std::memcpy(g_target, target.mac, 6);
    foxHuntReset();
    foxHuntRadioStart(target.channel ? target.channel : 1);
    tft.fillScreen(bruceConfig.bgColor);

    uint8_t campChannel = target.channel; // known from scan -> camp, no hopping
    uint8_t acquireChannel = target.channel ? target.channel : 1;
    uint32_t lastHop = millis(), lastRender = 0;
    int prevRssi = FOX_RSSI_FLOOR;
    bool havePrev = false;

    for (;;) {
        if (returnToMenu || check(EscPress)) break;
        const uint32_t now = millis();
        FoxSnapshot s = foxHuntSnapshot();
        if (campChannel == 0 && s.channel > 0) {
            campChannel = s.channel;
            esp_wifi_set_channel(campChannel, WIFI_SECOND_CHAN_NONE);
        } else if (campChannel == 0 && now - lastHop >= FOX_HOP_MS) {
            acquireChannel = nextAcquireChannel(acquireChannel);
            esp_wifi_set_channel(acquireChannel, WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }
        if (now - lastRender >= FOX_UI_MS) {
            foxHuntRender(target, s, campChannel ? campChannel : acquireChannel, prevRssi, havePrev);
            if (s.hasSample) {
                prevRssi = s.rssiEma;
                havePrev = true;
            }
            lastRender = now;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    foxHuntRadioStop();
    returnToMenu = true;
}

#endif // BRUCE_FOXHUNT
