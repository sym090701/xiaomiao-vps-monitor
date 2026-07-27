#pragma once

#include <Arduino.h>
#include <vector>

enum class BackendType : uint8_t {
    Nezha = 0,
    CfServerMonitor = 1,
};

struct AppConfig {
    String wifiSsid;
    String wifiPassword;
    BackendType backend = BackendType::Nezha;
    String baseUrl;
    String token;
    String cfServerIds;
    uint16_t refreshSeconds = 15;
    uint16_t offlineSeconds = 90;

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
    float load1 = 0;
    uint64_t uptimeSeconds = 0;
    uint64_t netInSpeed = 0;
    uint64_t netOutSpeed = 0;
    uint64_t netInTransfer = 0;
    uint64_t netOutTransfer = 0;
    int64_t lastActiveEpoch = 0;
};

struct FetchResult {
    bool success = false;
    String error;
    std::vector<ServerSnapshot> servers;
};
