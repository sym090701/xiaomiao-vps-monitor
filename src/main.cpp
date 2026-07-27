#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "api_client.h"
#include "config_store.h"
#include "hardware.h"
#include "monitor_display.h"
#include "monitor_types.h"
#include "web_config.h"

namespace {
AppConfig config;
std::vector<ServerSnapshot> snapshots;
String fetchNotice;
size_t selectedServer = 0;
uint8_t selectedPage = 0;
uint32_t lastFetchAt = 0;
uint32_t lastScreenRedraw = 0;
uint32_t lastReconnectAttempt = 0;
uint8_t lastWifiDisconnectReason = 0;
bool refreshRequested = false;
bool haveCompleteConfig = false;

Buttons previousButtons{};
uint32_t bPressedAt = 0;
uint32_t comboPressedAt = 0;
bool bLongHandled = false;
bool comboHandled = false;

void recordWifiDisconnect(WiFiEvent_t, WiFiEventInfo_t info) {
    lastWifiDisconnectReason = info.wifi_sta_disconnected.reason;
    Serial.printf("Wi-Fi disconnected: reason=%u\n", static_cast<unsigned>(lastWifiDisconnectReason));
}

void showPortal() {
    startConfigurationAccessPoint();
    displayProvisioning(configurationAccessPointSsid(),
                         configurationAccessPointPassword(),
                         WiFi.softAPIP().toString());
}

bool connectWifi() {
    if (!config.wifiSsid.length()) return false;
    displayConnecting(config.wifiSsid);
    lastWifiDisconnectReason = 0;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());

    const uint32_t deadline = millis() + 18000;
    while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(millis() - deadline) < 0) {
        serviceWebConfig();
        delay(50);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("Wi-Fi connection failed: status=%d reason=%u\n",
                      static_cast<int>(WiFi.status()),
                      static_cast<unsigned>(lastWifiDisconnectReason));
        return false;
    }

    Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());

    Serial.println("NTP sync: start");
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    const uint32_t timeDeadline = millis() + 5000;
    while (time(nullptr) < 1700000000 && static_cast<int32_t>(millis() - timeDeadline) < 0) {
        serviceWebConfig();
        delay(50);
    }
    Serial.printf("NTP sync: done valid=%s\n", time(nullptr) >= 1700000000 ? "yes" : "no");
    return true;
}

void redrawSnapshot() {
    if (snapshots.empty()) return;
    if (selectedServer >= snapshots.size()) selectedServer = 0;
    displaySnapshot(snapshots[selectedServer], selectedServer, snapshots.size(),
                    selectedPage, fetchNotice);
    lastScreenRedraw = millis();
}

void refreshData() {
    if (!haveCompleteConfig) return;
    if (WiFi.status() != WL_CONNECTED) {
        fetchNotice = "STALE: Wi-Fi offline";
        if (snapshots.empty()) displayMessage("WI-FI OFFLINE", "Long press B to open configuration.", TFT_RED);
        else redrawSnapshot();
        return;
    }

    if (!snapshots.empty()) displayRefreshing();
    else displayBoot("Fetching VPS data", config.backend == BackendType::Nezha ? "Nezha" : "CF Server Monitor", "Please wait...");

    Serial.println("Monitor fetch: start");
    FetchResult result = fetchServerSnapshots(config);
    Serial.println("Monitor fetch: returned");
    lastFetchAt = millis();
    refreshRequested = false;

    if (result.success) {
        snapshots.swap(result.servers);
        if (selectedServer >= snapshots.size()) selectedServer = 0;
        fetchNotice = result.error;
        redrawSnapshot();
        Serial.printf("Monitor refresh: %u server(s), free heap %u, free PSRAM %u\n",
                      static_cast<unsigned>(snapshots.size()),
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getFreePsram()));
        return;
    }

    fetchNotice = "STALE: " + result.error;
    Serial.printf("Monitor refresh failed: %s\n", result.error.c_str());
    if (snapshots.empty()) {
        displayMessage("FETCH FAILED", result.error + "\n\nLong B: configuration", TFT_RED);
    } else {
        redrawSnapshot();
    }
}

void handleButtons() {
    const Buttons current = readButtons();
    const uint32_t now = millis();

    if (current.a && current.b) {
        if (!previousButtons.a || !previousButtons.b) {
            comboPressedAt = now;
            comboHandled = false;
        }
        if (!comboHandled && now - comboPressedAt >= 1500) {
            comboHandled = true;
            displayBoot("Restarting...", "Press A at Launcher", "boot screen to enter");
            delay(250);
            ESP.restart();
        }
    } else {
        comboPressedAt = 0;
        comboHandled = false;
    }

    if (current.b && !current.a) {
        if (!previousButtons.b) {
            bPressedAt = now;
            bLongHandled = false;
        }
        if (!bLongHandled && now - bPressedAt >= 1500) {
            bLongHandled = true;
            showPortal();
        }
    } else if (!current.b) {
        bPressedAt = 0;
        bLongHandled = false;
    }

    const bool anyCombo = current.a && current.b;
    if (!anyCombo && current.left && !previousButtons.left && !snapshots.empty()) {
        selectedServer = (selectedServer + snapshots.size() - 1) % snapshots.size();
        redrawSnapshot();
    }
    if (!anyCombo && current.right && !previousButtons.right && !snapshots.empty()) {
        selectedServer = (selectedServer + 1) % snapshots.size();
        redrawSnapshot();
    }
    if (!anyCombo && current.up && !previousButtons.up) {
        selectedPage = (selectedPage + 1) % 2;
        redrawSnapshot();
    }
    if (!anyCombo && current.down && !previousButtons.down) {
        selectedPage = (selectedPage + 1) % 2;
        redrawSnapshot();
    }
    if (!anyCombo && current.a && !previousButtons.a) refreshRequested = true;

    previousButtons = current;
}

void maintainWifi() {
    if (!haveCompleteConfig || WiFi.status() == WL_CONNECTED) return;
    if (millis() - lastReconnectAttempt < 10000) return;
    lastReconnectAttempt = millis();
    WiFi.reconnect();
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(150);
    initHardwarePins();
    displayInit();
    displayBoot("Starting firmware", "XiaoMiao ESP32", "VPS Monitor 1.0");

    haveCompleteConfig = loadConfig(config);

    // WebServer uses the TCP/IP mailbox immediately in begin(). On a fresh
    // Launcher app boot, initialize Arduino Wi-Fi/LwIP before binding port 80.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.onEvent(recordWifiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    startWebConfig(config);

    Serial.printf("PSRAM: %s, size %u bytes\n", psramFound() ? "ready" : "not found",
                  static_cast<unsigned>(ESP.getPsramSize()));

    if (!haveCompleteConfig) {
        showPortal();
        return;
    }

    if (!connectWifi()) {
        showPortal();
        return;
    }

    refreshRequested = true;
}

void loop() {
    serviceWebConfig();
    handleButtons();
    maintainWifi();

    if (haveCompleteConfig && WiFi.status() == WL_CONNECTED) {
        const uint32_t intervalMs = static_cast<uint32_t>(config.refreshSeconds) * 1000;
        if (refreshRequested || lastFetchAt == 0 || millis() - lastFetchAt >= intervalMs) {
            refreshData();
        }
    }

    if (!snapshots.empty() && millis() - lastScreenRedraw >= 5000 &&
        !configurationAccessPointActive()) {
        redrawSnapshot();
    }
    delay(25);
}
