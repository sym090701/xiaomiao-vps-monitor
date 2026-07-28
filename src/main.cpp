#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <time.h>
#include <algorithm>

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
String alertNotice;
size_t selectedServer = 0;
uint8_t selectedPage = 0;
uint32_t lastFetchAt = 0;
uint32_t lastScreenRedraw = 0;
uint32_t lastReconnectAttempt = 0;
uint8_t lastWifiDisconnectReason = 0;
bool refreshRequested = false;
bool haveCompleteConfig = false;

Buttons previousButtons{};
uint32_t aPressedAt = 0;
uint32_t bPressedAt = 0;
uint32_t comboPressedAt = 0;
uint32_t lastUserInputAt = 0;
bool aShortSuppressed = false;
bool bShortSuppressed = false;
bool bLongHandled = false;
bool comboHandled = false;

struct AlertTracker {
    String id;
    uint32_t cpuSince = 0;
    uint32_t memorySince = 0;
    uint32_t diskSince = 0;
    uint32_t offlineSince = 0;
    bool cpuActive = false;
    bool memoryActive = false;
    bool diskActive = false;
    bool offlineActive = false;
};

std::vector<AlertTracker> alertTrackers;

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
    const uint8_t pages = displayPageCount(snapshots[selectedServer]);
    if (selectedPage >= pages) selectedPage = pages - 1;
    String notice = alertNotice.length() ? alertNotice : fetchNotice;
    if (fetchNotice.startsWith("STALE:")) {
        notice = fetchNotice;
        if (alertNotice.length()) notice += " + ALERT";
    }
    displayMonitor(snapshots, selectedServer, selectedPage, notice);
    lastScreenRedraw = millis();
}

void resetPendingAlerts() {
    for (auto &tracker : alertTrackers) {
        if (!tracker.cpuActive) tracker.cpuSince = 0;
        if (!tracker.memoryActive) tracker.memorySince = 0;
        if (!tracker.diskActive) tracker.diskSince = 0;
        if (!tracker.offlineActive) tracker.offlineSince = 0;
    }
}

bool updateAlertTimer(bool violated, uint32_t now, uint32_t durationMs,
                      uint32_t &startedAt, bool &active) {
    const bool wasActive = active;
    if (!violated) {
        startedAt = 0;
        active = false;
    } else {
        if (!startedAt) startedAt = now ? now : 1;
        if (now - startedAt >= durationMs) active = true;
    }
    return !wasActive && active;
}

int evaluateAlerts() {
    const uint32_t now = millis();
    const uint32_t durationMs = static_cast<uint32_t>(config.alertDurationSeconds) * 1000;
    int firstNewAlert = -1;
    alertNotice = "";
    size_t activeCount = 0;
    String firstAlert;

    for (size_t index = 0; index < snapshots.size(); index++) {
        const auto &snapshot = snapshots[index];
        auto found = std::find_if(alertTrackers.begin(), alertTrackers.end(),
                                  [&](const AlertTracker &tracker) { return tracker.id == snapshot.id; });
        if (found == alertTrackers.end()) {
            alertTrackers.push_back(AlertTracker{});
            found = alertTrackers.end() - 1;
            found->id = snapshot.id;
        }
        AlertTracker &tracker = *found;
        bool nodeNewAlert = false;
        nodeNewAlert |= updateAlertTimer(config.cpuAlertPercent && snapshot.cpuPercent >= config.cpuAlertPercent,
                                         now, durationMs, tracker.cpuSince, tracker.cpuActive);
        nodeNewAlert |= updateAlertTimer(config.memoryAlertPercent && snapshot.memoryPercent >= config.memoryAlertPercent,
                                         now, durationMs, tracker.memorySince, tracker.memoryActive);
        nodeNewAlert |= updateAlertTimer(config.diskAlertPercent && snapshot.diskPercent >= config.diskAlertPercent,
                                         now, durationMs, tracker.diskSince, tracker.diskActive);
        nodeNewAlert |= updateAlertTimer(!snapshot.online, now, durationMs,
                                         tracker.offlineSince, tracker.offlineActive);
        if (nodeNewAlert && firstNewAlert < 0) firstNewAlert = static_cast<int>(index);

        String metrics;
        if (tracker.offlineActive) metrics += "OFF ";
        if (tracker.cpuActive) metrics += "CPU ";
        if (tracker.memoryActive) metrics += "RAM ";
        if (tracker.diskActive) metrics += "DSK ";
        if (metrics.length()) {
            activeCount++;
            if (!firstAlert.length()) firstAlert = snapshot.name + " " + metrics;
        }
    }

    alertTrackers.erase(std::remove_if(alertTrackers.begin(), alertTrackers.end(), [&](const AlertTracker &tracker) {
        return std::none_of(snapshots.begin(), snapshots.end(),
                            [&](const ServerSnapshot &snapshot) { return snapshot.id == tracker.id; });
    }), alertTrackers.end());

    if (activeCount) {
        alertNotice = "ALERT " + firstAlert;
        if (activeCount > 1) alertNotice += "+" + String(activeCount - 1);
    }
    return firstNewAlert;
}

void refreshData() {
    if (!haveCompleteConfig) return;
    if (WiFi.status() != WL_CONNECTED) {
        resetPendingAlerts();
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
        const String selectedId = selectedServer < snapshots.size()
                                      ? snapshots[selectedServer].id
                                      : "";
        snapshots.swap(result.servers);
        selectedServer = 0;
        if (selectedId.length()) {
            const auto selected = std::find_if(snapshots.begin(), snapshots.end(),
                                               [&](const ServerSnapshot &snapshot) {
                                                   return snapshot.id == selectedId;
                                               });
            if (selected != snapshots.end()) {
                selectedServer = static_cast<size_t>(selected - snapshots.begin());
            }
        }
        fetchNotice = result.error;
        const int newAlertIndex = evaluateAlerts();
        if (newAlertIndex >= 0 && millis() - lastUserInputAt >= 3000) {
            selectedServer = static_cast<size_t>(newAlertIndex);
            selectedPage = 0;
        }
        redrawSnapshot();
        Serial.printf("Monitor refresh: %u server(s), free heap %u, free PSRAM %u\n",
                      static_cast<unsigned>(snapshots.size()),
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getFreePsram()));
        return;
    }

    fetchNotice = "STALE: " + result.error;
    resetPendingAlerts();
    Serial.printf("Monitor refresh failed: %s\n", result.error.c_str());
    if (snapshots.empty()) {
        displayMessage("FETCH FAILED", result.error + "\n\nLong B: configuration", TFT_RED);
    } else {
        redrawSnapshot();
    }
}

void selectServer(int direction) {
    if (snapshots.empty()) return;
    if (selectedPage == 0) {
        if (direction < 0 && selectedServer > 0) selectedServer--;
        if (direction > 0 && selectedServer + 1 < snapshots.size()) selectedServer++;
    } else {
        selectedServer = direction < 0
                             ? (selectedServer + snapshots.size() - 1) % snapshots.size()
                             : (selectedServer + 1) % snapshots.size();
        const uint8_t pages = displayPageCount(snapshots[selectedServer]);
        if (selectedPage >= pages) selectedPage = pages - 1;
        if (selectedPage == 0) selectedPage = 1;
    }
    redrawSnapshot();
}

void handleShortA() {
    if (!snapshots.empty() && selectedPage == 0) {
        selectedPage = 1;
        redrawSnapshot();
    } else {
        refreshRequested = true;
    }
}

void handleShortB() {
    if (!snapshots.empty() && selectedPage != 0) {
        selectedPage = 0;
        redrawSnapshot();
    } else {
        refreshRequested = true;
    }
}

void handleButtons() {
    const Buttons current = readButtons();
    const uint32_t now = millis();
    const bool currentCombo = current.a && current.b;
    const bool previousCombo = previousButtons.a && previousButtons.b;
    const bool anyNewPress = (current.up && !previousButtons.up) ||
                             (current.down && !previousButtons.down) ||
                             (current.left && !previousButtons.left) ||
                             (current.right && !previousButtons.right) ||
                             (current.a && !previousButtons.a) ||
                             (current.b && !previousButtons.b);
    if (anyNewPress) lastUserInputAt = now;

    if (currentCombo) {
        aShortSuppressed = true;
        bShortSuppressed = true;
        if (!previousCombo) {
            comboPressedAt = now;
            comboHandled = false;
        }
        if (!comboHandled && now - comboPressedAt >= 1500) {
            comboHandled = true;
            displayBoot("Restarting...", "Press A at Launcher", "boot screen to enter");
            delay(250);
            ESP.restart();
        }
    } else if (!current.a && !current.b) {
        comboPressedAt = 0;
        comboHandled = false;
    }

    if (current.a && !previousButtons.a) {
        aPressedAt = now;
        aShortSuppressed = current.b;
    }
    if (!current.a && previousButtons.a) {
        if (aPressedAt && !aShortSuppressed) handleShortA();
        aPressedAt = 0;
        aShortSuppressed = false;
    }

    if (current.b && !previousButtons.b) {
        bPressedAt = now;
        bLongHandled = false;
        bShortSuppressed = current.a;
    }
    if (current.b && !current.a && bPressedAt && !bShortSuppressed) {
        if (!bLongHandled && now - bPressedAt >= 1500) {
            bLongHandled = true;
            bShortSuppressed = true;
            showPortal();
        }
    }
    if (!current.b && previousButtons.b) {
        if (bPressedAt && !bLongHandled && !bShortSuppressed) handleShortB();
        bPressedAt = 0;
        bShortSuppressed = false;
        bLongHandled = false;
    }

    const bool commandButtonDown = current.a || current.b;
    if (!commandButtonDown && current.left && !previousButtons.left) selectServer(-1);
    if (!commandButtonDown && current.right && !previousButtons.right) selectServer(1);
    if (!commandButtonDown && current.up && !previousButtons.up && !snapshots.empty()) {
        if (selectedPage == 0) selectServer(-1);
        else if (selectedPage > 1) {
            selectedPage--;
            redrawSnapshot();
        }
    }
    if (!commandButtonDown && current.down && !previousButtons.down && !snapshots.empty()) {
        if (selectedPage == 0) selectServer(1);
        else {
            const uint8_t pages = displayPageCount(snapshots[selectedServer]);
            if (selectedPage + 1 < pages) {
                selectedPage++;
                redrawSnapshot();
            }
        }
    }

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
    displayBoot("Starting firmware", "XiaoMiao ESP32", "VPS Monitor 1.1");

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
