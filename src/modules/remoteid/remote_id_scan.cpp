// Drone Remote-ID (OpenDroneID) WiFi capture for Bruce. See remote_id_scan.h.
// Merge/lifecycle logic adapted from ESP32Marauder's WiFiScanRemoteId.h; WiFi
// promiscuous setup/teardown mirrors Bruce's src/modules/wifi/sniffer.cpp.
#include "remote_id_scan.h"

#ifdef BRUCE_REMOTEID

#include <Arduino.h>

#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <cstring>

#include "RemoteIdDecoder.h"
#include "RemoteIdModel.h"
#include "core/display.h"          // tft, drawMainBorderWithTitle, bruceConfig, tftWidth/Height
#include "core/mykeyboard.h"       // check(), EscPress
#include "core/net_utils.h"        // macToString
#include "core/wifi/wifi_common.h" // ensureWifiPlatform, wifiDisconnect
#include <globals.h>               // returnToMenu

// Detection store capacity: generous on PSRAM boards, small otherwise.
#ifdef BOARD_HAS_PSRAM
static constexpr size_t REMOTE_ID_CAPACITY = 48;
#else
static constexpr size_t REMOTE_ID_CAPACITY = 12;
#endif
static constexpr uint32_t REMOTE_ID_STALE_MS = 30000;
static constexpr uint32_t REMOTE_ID_SCAN_DEFAULT_MS = 30000;
static constexpr uint32_t REMOTE_ID_HOP_MS = 250;

// 2.4GHz channel plan, CH6 first (ASTM-preferred NAN channel). Dual-band/5GHz
// is a later enhancement.
static const uint8_t kChannels[] = {6, 1, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static constexpr size_t kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);

static RemoteIdRecord g_records[REMOTE_ID_CAPACITY];
static RemoteIdStore g_store(g_records, REMOTE_ID_CAPACITY);
static RemoteIdDecoder g_decoder;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// --- merge/observe helpers (adapted from ESP32Marauder WiFiScanRemoteId.h) ---

static void copyIfPresent(char *destination, size_t destinationSize, const char *source, bool present) {
    if (!present) return;
    std::strncpy(destination, source, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}

static void updateKnownObservation(
    RemoteIdRecord &record, const uint8_t mac[6], RemoteIdTransport transport, int8_t rssi, uint32_t now
) {
    memcpy(record.mac, mac, sizeof(record.mac));
    record.transportMask |= static_cast<uint8_t>(transport);
    record.rssi = rssi;
    if (record.isLost) {
        record.isLost = false;
        ++record.reacquiredCount;
        record.lastReacquiredMs = now;
    }
    record.lastSeenMs = now;
    ++record.packetCount;
    if (record.rateWindowStartedMs == 0) record.rateWindowStartedMs = now;
    ++record.rateWindowPackets;
    const uint32_t elapsed = now - record.rateWindowStartedMs;
    if (elapsed >= 1000) {
        record.packetRateHz = record.rateWindowPackets * 1000.0f / static_cast<float>(elapsed);
        record.rateWindowStartedMs = now;
        record.rateWindowPackets = 0;
    }
}

static void mergeRecord(RemoteIdRecord &destination, const RemoteIdRecord &source) {
    const bool acceptUasId =
        source.hasUasId &&
        remoteIdShouldReplaceBasicId(destination.idType, destination.hasUasId, source.idType);
    copyIfPresent(destination.uasId, sizeof(destination.uasId), source.uasId, acceptUasId);
    copyIfPresent(destination.operatorId, sizeof(destination.operatorId), source.operatorId, source.hasOperatorId);
    copyIfPresent(destination.description, sizeof(destination.description), source.description, source.hasDescription);
    if (acceptUasId) {
        destination.hasUasId = true;
        destination.idType = source.idType;
        destination.uaType = source.uaType;
    }
    if (source.hasOperatorId) destination.hasOperatorId = true;
    if (source.hasDescription) destination.hasDescription = true;
    if (source.hasLocation) {
        destination.hasLocation = true;
        destination.latitudeE7 = source.latitudeE7;
        destination.longitudeE7 = source.longitudeE7;
        destination.altitudeGeoM = source.altitudeGeoM;
        destination.altitudePressureM = source.altitudePressureM;
        destination.heightM = source.heightM;
        destination.horizontalSpeedMps = source.horizontalSpeedMps;
        destination.verticalSpeedMps = source.verticalSpeedMps;
        destination.directionDeg = source.directionDeg;
        destination.operationalStatus = source.operationalStatus;
        destination.horizontalAccuracy = source.horizontalAccuracy;
        destination.verticalAccuracy = source.verticalAccuracy;
        destination.speedAccuracy = source.speedAccuracy;
        destination.locationTimestampDeciseconds = source.locationTimestampDeciseconds;
        destination.heightIsAboveGround = source.heightIsAboveGround;
        destination.directionValid = source.directionValid;
        destination.horizontalSpeedValid = source.horizontalSpeedValid;
        destination.verticalSpeedValid = source.verticalSpeedValid;
        destination.altitudePressureValid = source.altitudePressureValid;
        destination.altitudeGeoValid = source.altitudeGeoValid;
        destination.heightValid = source.heightValid;
        destination.locationTimestampValid = source.locationTimestampValid;
    }
    if (source.hasAuthentication) {
        destination.hasAuthentication = true;
        destination.authenticationType = source.authenticationType;
        destination.authenticationPage = source.authenticationPage;
        destination.authenticationLastPage = source.authenticationLastPage;
        destination.authenticationLength = source.authenticationLength;
        destination.authenticationTimestamp = source.authenticationTimestamp;
    }
    if (source.hasOperatorLocation) {
        destination.hasOperatorLocation = true;
        destination.operatorLatitudeE7 = source.operatorLatitudeE7;
        destination.operatorLongitudeE7 = source.operatorLongitudeE7;
        destination.operatorLocationType = source.operatorLocationType;
        destination.classificationType = source.classificationType;
        destination.areaCount = source.areaCount;
        destination.areaRadiusM = source.areaRadiusM;
        destination.areaCeilingM = source.areaCeilingM;
        destination.areaFloorM = source.areaFloorM;
        destination.areaCeilingValid = source.areaCeilingValid;
        destination.areaFloorValid = source.areaFloorValid;
        destination.categoryEu = source.categoryEu;
        destination.classEu = source.classEu;
        destination.operatorAltitudeGeoM = source.operatorAltitudeGeoM;
        destination.operatorAltitudeValid = source.operatorAltitudeValid;
        destination.systemTimestamp = source.systemTimestamp;
    }
}

// Decode a management frame and merge the result into the store. The decode runs
// outside the critical section; only the store mutation is locked.
static void processWifiFrame(
    const uint8_t *frame, size_t length, const uint8_t mac[6], int8_t rssi, uint8_t channel
) {
    RemoteIdRecord decoded;
    RemoteIdTransport transport = RemoteIdTransport::WifiBeacon;
    RemoteIdDecodeResult result = g_decoder.decodeWifiBeacon(frame, length, decoded);
    if (result != RemoteIdDecodeResult::Decoded) {
        result = g_decoder.decodeWifiNan(frame, length, decoded);
        transport = RemoteIdTransport::WifiNan;
    }
    if (result != RemoteIdDecodeResult::Decoded) return;

    const uint32_t now = millis();
    portENTER_CRITICAL(&g_mux);
    RemoteIdRecord *record = decoded.hasUasId ? g_store.findByUasId(decoded.uasId) : nullptr;
    RemoteIdRecord *provisional = g_store.findByMac(mac);
    if (record == nullptr) {
        record = &g_store.observe(mac, transport, rssi, now);
    } else {
        if (provisional != nullptr && provisional != record) {
            mergeRecord(*record, *provisional);
            record->transportMask |= provisional->transportMask;
            record->packetCount += provisional->packetCount;
            record->lostCount += provisional->lostCount;
            record->reacquiredCount += provisional->reacquiredCount;
            g_store.eraseByMac(mac, record);
            record = g_store.findByUasId(decoded.uasId);
            if (record == nullptr) {
                portEXIT_CRITICAL(&g_mux);
                return;
            }
        }
        updateKnownObservation(*record, mac, transport, rssi, now);
    }
    mergeRecord(*record, decoded);
    record->lastWifiChannel = channel;
    portEXIT_CRITICAL(&g_mux);
}

static void remoteIdWifiCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || buf == nullptr) return;
    wifi_promiscuous_pkt_t *packet = static_cast<wifi_promiscuous_pkt_t *>(buf);
    size_t length = packet->rx_ctrl.sig_len;
    if (length >= 4) length -= 4; // strip FCS
    // Beacon (0x80) or action/NAN (0xD0) management frames only.
    if (length < 24 || (packet->payload[0] != 0x80 && packet->payload[0] != 0xD0)) return;
    // Transmitter address is at offset 10 in the 802.11 management header.
    processWifiFrame(
        packet->payload, length, packet->payload + 10, packet->rx_ctrl.rssi, packet->rx_ctrl.channel
    );
}

size_t remoteIdSnapshot(RemoteIdRecord *out, size_t capacity) {
    if (out == nullptr || capacity == 0) return 0;
    portENTER_CRITICAL(&g_mux);
    const size_t count = g_store.size() < capacity ? g_store.size() : capacity;
    for (size_t i = 0; i < count; ++i) {
        const RemoteIdRecord *rec = g_store.at(i);
        if (rec != nullptr) out[i] = *rec;
    }
    portEXIT_CRITICAL(&g_mux);
    return count;
}

size_t remoteIdDetectedCount() {
    portENTER_CRITICAL(&g_mux);
    const size_t count = g_store.size();
    portEXIT_CRITICAL(&g_mux);
    return count;
}

static void dumpRecord(const RemoteIdRecord &r) {
    char transports[16];
    remoteIdFormatTransports(r.transportMask, transports, sizeof(transports));
    String id = r.hasUasId ? String(r.uasId) : macToString(r.mac);
    String line = "RID," + id + "," + (r.hasOperatorId ? String(r.operatorId) : String("")) + ",[" +
                  String(transports) + "],rssi=" + String(r.rssi) + ",ch=" + String(r.lastWifiChannel) +
                  ",pkts=" + String(r.packetCount);
    if (r.hasLocation) {
        line += ",lat=" + String(r.latitudeE7 / 1e7, 7) + ",lon=" + String(r.longitudeE7 / 1e7, 7) +
                ",alt=" + String(r.altitudeGeoM, 1) + "m";
    }
    Serial.println(line);
}

// --- shared radio lifecycle (mirrors sniffer.cpp) ---

static void remoteIdStoreReset() {
    portENTER_CRITICAL(&g_mux);
    g_store.clear();
    portEXIT_CRITICAL(&g_mux);
}

static void remoteIdRadioStart() {
    ensureWifiPlatform();
    nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL)); // passive: no beacon TX
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(remoteIdWifiCallback);
    esp_wifi_set_channel(kChannels[0], WIFI_SECOND_CHAN_NONE);
}

static void remoteIdRadioStop() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_set_promiscuous_rx_cb(NULL);
    wifiDisconnect();
}

void remoteIdScanRun(uint32_t durationMs) {
    if (durationMs == 0) durationMs = REMOTE_ID_SCAN_DEFAULT_MS;

    remoteIdStoreReset();
    remoteIdRadioStart();

    Serial.printf("[RID] scan started (%us); press any key to stop\n", durationMs / 1000);

    const uint32_t start = millis();
    uint32_t lastHop = start, lastTick = start, lastPrune = start;
    size_t chIdx = 0;
    while (millis() - start < durationMs) {
        const uint32_t now = millis();
        if (now - lastHop >= REMOTE_ID_HOP_MS) {
            chIdx = (chIdx + 1) % kChannelCount;
            esp_wifi_set_channel(kChannels[chIdx], WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }
        if (now - lastPrune >= 1000) {
            portENTER_CRITICAL(&g_mux);
            g_store.updateLifecycle(now, REMOTE_ID_STALE_MS);
            portEXIT_CRITICAL(&g_mux);
            lastPrune = now;
        }
        if (now - lastTick >= 1000) {
            Serial.printf("[RID] ch=%u detected=%u\n", kChannels[chIdx], (unsigned)remoteIdDetectedCount());
            lastTick = now;
        }
        if (Serial.available()) {
            while (Serial.available()) Serial.read();
            Serial.println("[RID] stopped by key");
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    remoteIdRadioStop();

    // Dump detections (radio is stopped, so the store is stable: no lock needed
    // across the whole loop, but copy each record under the lock defensively).
    const size_t count = remoteIdDetectedCount();
    Serial.printf("[RID] scan done, %u drone(s) detected\n", (unsigned)count);
    for (size_t i = 0; i < count; ++i) {
        RemoteIdRecord rec;
        bool ok = false;
        portENTER_CRITICAL(&g_mux);
        const RemoteIdRecord *p = g_store.at(i);
        if (p != nullptr) {
            rec = *p;
            ok = true;
        }
        portEXIT_CRITICAL(&g_mux);
        if (ok) dumpRecord(rec);
    }
}

// --- on-device live list display ---

static void remoteIdRenderList(uint8_t channel) {
    tft.fillScreen(bruceConfig.bgColor);
    drawMainBorderWithTitle("Remote ID");
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    const int16_t x = 8;
    const int16_t lineH = LH * FP + 2;
    int16_t y = BORDER_PAD_Y + FM * LH;

    const size_t count = remoteIdDetectedCount();
    tft.setCursor(x, y);
    tft.print("Drones:" + String((unsigned)count) + "  CH:" + String(channel));
    y += lineH + 2;

    const int16_t bottom = tftHeight - lineH - 4; // leave room for footer
    char transports[16];
    for (size_t i = 0; i < count && y < bottom; ++i) {
        RemoteIdRecord r;
        bool ok = false;
        portENTER_CRITICAL(&g_mux);
        const RemoteIdRecord *p = g_store.at(i);
        if (p != nullptr) {
            r = *p;
            ok = true;
        }
        portEXIT_CRITICAL(&g_mux);
        if (!ok) continue;
        remoteIdFormatTransports(r.transportMask, transports, sizeof(transports));
        String id = r.hasUasId ? String(r.uasId) : macToString(r.mac);
        tft.setTextColor(r.isLost ? TFT_DARKGREY : bruceConfig.priColor, bruceConfig.bgColor);
        tft.setCursor(x, y);
        tft.print(String(r.rssi) + " [" + String(transports) + "] " + id);
        y += lineH;
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(x, tftHeight - lineH - 2);
    tft.print("ESC: exit");
}

void remoteIdScanScreen() {
    returnToMenu = false;
    remoteIdStoreReset();
    remoteIdRadioStart();
    tft.fillScreen(bruceConfig.bgColor);

    size_t chIdx = 0;
    uint32_t lastHop = millis(), lastPrune = millis(), lastRender = 0;
    for (;;) {
        if (returnToMenu) break;
        if (check(EscPress)) {
            returnToMenu = true;
            break;
        }
        const uint32_t now = millis();
        if (now - lastHop >= REMOTE_ID_HOP_MS) {
            chIdx = (chIdx + 1) % kChannelCount;
            esp_wifi_set_channel(kChannels[chIdx], WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }
        if (now - lastPrune >= 1000) {
            portENTER_CRITICAL(&g_mux);
            g_store.updateLifecycle(now, REMOTE_ID_STALE_MS);
            portEXIT_CRITICAL(&g_mux);
            lastPrune = now;
        }
        if (now - lastRender >= 500) {
            remoteIdRenderList(kChannels[chIdx]);
            lastRender = now;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    remoteIdRadioStop();
}

#endif // BRUCE_REMOTEID
