#pragma once

#include <Arduino.h>
#include <vector>

enum class BackendType : uint8_t {
    Nezha = 0,
    CfServerMonitor = 1,
};

enum class MonitorPage : uint8_t {
    Overview = 0,
    Summary,
    Network,
    Advanced,
    Plan,
    Trends,
    Diagnostics,
};

constexpr uint8_t TREND_SAMPLE_COUNT = 24;
constexpr char FIRMWARE_VERSION[] = "1.2.0";

struct AppConfig {
    String wifiSsid;
    String wifiPassword;
    BackendType backend = BackendType::Nezha;
    String baseUrl;
    String token;
    String cfServerIds;
    uint16_t refreshSeconds = 15;
    uint16_t offlineSeconds = 90;
    uint8_t cpuAlertPercent = 90;
    uint8_t memoryAlertPercent = 90;
    uint8_t diskAlertPercent = 90;
    uint16_t alertDurationSeconds = 60;

    bool isComplete() const {
        return wifiSsid.length() > 0 && baseUrl.length() > 0;
    }
};

struct ServerSnapshot {
    String id;
    String name;
    String platform;
    String arch;
    String uptimeText;
    bool online = false;
    float cpuPercent = 0;
    float memoryPercent = 0;
    float diskPercent = 0;
    float swapPercent = 0;
    float load1 = 0;
    float load5 = 0;
    float load15 = 0;
    float maxTemperature = 0;
    uint32_t processCount = 0;
    uint32_t tcpConnections = 0;
    uint32_t udpConnections = 0;
    String virtualization;
    uint64_t uptimeSeconds = 0;
    uint64_t netInSpeed = 0;
    uint64_t netOutSpeed = 0;
    uint64_t netInTransfer = 0;
    uint64_t netOutTransfer = 0;
    uint64_t monthlyNetIn = 0;
    uint64_t monthlyNetOut = 0;
    uint64_t trafficLimitBytes = 0;
    String trafficLimitText;
    String expiryDate;
    int32_t expiryDays = INT32_MIN;
    String price;
    String serverGroup;
    uint8_t trafficResetDay = 0;
    int64_t lastActiveEpoch = 0;
    bool hasAdvancedMetrics = false;
    bool hasPlanMetrics = false;
};

struct ServerTrends {
    String id;
    uint8_t count = 0;
    uint8_t next = 0;
    float cpu[TREND_SAMPLE_COUNT]{};
    float memory[TREND_SAMPLE_COUNT]{};
    uint64_t network[TREND_SAMPLE_COUNT]{};
};

struct DeviceDiagnostics {
    bool wifiConnected = false;
    int32_t wifiRssi = 0;
    String localIp;
    uint32_t requestDurationMs = 0;
    uint32_t lastRefreshAgeSeconds = UINT32_MAX;
    uint32_t freeHeap = 0;
    uint32_t freePsram = 0;
    uint32_t deviceUptimeSeconds = 0;
};

struct FetchResult {
    bool success = false;
    String error;
    std::vector<ServerSnapshot> servers;
};

struct ConnectionTestResult {
    bool success = false;
    std::vector<String> stages;
};
