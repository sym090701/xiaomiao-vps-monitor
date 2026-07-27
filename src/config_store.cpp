#include "config_store.h"

#include <Preferences.h>

namespace {
constexpr const char *NAMESPACE_NAME = "vpsmon";
}

bool loadConfig(AppConfig &config) {
    Preferences prefs;
    // A first-run read-only open reports NVS_NOT_FOUND because the private
    // namespace has not been created yet. Opening read-write creates only our
    // namespace in Launcher's already-present shared NVS partition.
    if (!prefs.begin(NAMESPACE_NAME, false)) return false;

    config.wifiSsid = prefs.getString("ssid", "");
    config.wifiPassword = prefs.getString("wifi_pass", "");
    config.backend = prefs.getUChar("backend", 0) == 1
                         ? BackendType::CfServerMonitor
                         : BackendType::Nezha;
    config.baseUrl = prefs.getString("url", "");
    config.token = prefs.getString("token", "");
    config.cfServerIds = prefs.getString("cf_ids", "");
    config.refreshSeconds = prefs.getUShort("refresh", 15);
    config.offlineSeconds = prefs.getUShort("offline", 90);
    prefs.end();

    config.refreshSeconds = constrain(config.refreshSeconds, 5, 3600);
    config.offlineSeconds = constrain(config.offlineSeconds, 15, 3600);
    return config.isComplete();
}

bool saveConfig(const AppConfig &config) {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE_NAME, false)) return false;

    bool ok = true;
    ok &= prefs.putString("ssid", config.wifiSsid) > 0;
    if (config.wifiPassword.length() == 0) {
        prefs.remove("wifi_pass");
    } else {
        ok &= prefs.putString("wifi_pass", config.wifiPassword) > 0;
    }
    ok &= prefs.putUChar("backend", config.backend == BackendType::CfServerMonitor ? 1 : 0) == 1;
    ok &= prefs.putString("url", config.baseUrl) > 0;
    if (config.token.length() == 0) {
        prefs.remove("token");
    } else {
        ok &= prefs.putString("token", config.token) > 0;
    }
    if (config.cfServerIds.length() == 0) {
        prefs.remove("cf_ids");
    } else {
        ok &= prefs.putString("cf_ids", config.cfServerIds) > 0;
    }
    ok &= prefs.putUShort("refresh", constrain(config.refreshSeconds, 5, 3600)) == 2;
    ok &= prefs.putUShort("offline", constrain(config.offlineSeconds, 15, 3600)) == 2;
    prefs.end();
    return ok;
}

void clearConfig() {
    Preferences prefs;
    if (!prefs.begin(NAMESPACE_NAME, false)) return;
    prefs.clear();
    prefs.end();
}
