#include "api_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <time.h>

#include <algorithm>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

namespace {
constexpr size_t MAX_RESPONSE_BYTES = 512 * 1024;
constexpr size_t MAX_SERVERS = 128;

String trimBaseUrl(String url) {
    url.trim();
    while (url.endsWith("/")) url.remove(url.length() - 1);
    return url;
}

String basicAuthValue(const String &secret) {
    String raw = "admin:" + secret;
    size_t outputLength = 0;
    const size_t capacity = 4 * ((raw.length() + 2) / 3) + 1;
    std::vector<unsigned char> output(capacity);
    if (mbedtls_base64_encode(output.data(), output.size(), &outputLength,
                              reinterpret_cast<const unsigned char *>(raw.c_str()),
                              raw.length()) != 0) {
        return "";
    }
    output[outputLength] = '\0';
    return "Basic " + String(reinterpret_cast<const char *>(output.data()));
}

bool readHttpBody(HTTPClient &http, String &body, String &error) {
    const int contentLength = http.getSize();
    if (contentLength > static_cast<int>(MAX_RESPONSE_BYTES)) {
        error = "response too large";
        return false;
    }
    body = http.getString();
    if (body.length() > MAX_RESPONSE_BYTES) {
        body = "";
        error = "response too large";
        return false;
    }
    return true;
}

bool httpGet(const String &url, const String &authorization, String &body,
             int &status, String &error) {
    Serial.printf("HTTP GET: prepare %s\n", url.c_str());
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(12000);
    // Never forward a monitoring credential to a redirect target.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent("XiaoMiao-VPS-Monitor/" + String(FIRMWARE_VERSION));

    bool begun = false;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    if (url.startsWith("https://")) {
        Serial.println("HTTP GET: configure TLS CA bundle");
        secureClient.setCACertBundle(rootca_crt_bundle_start);
        begun = http.begin(secureClient, url);
    } else if (url.startsWith("http://")) {
        begun = http.begin(plainClient, url);
    } else {
        error = "URL must start http:// or https://";
        return false;
    }

    if (!begun) {
        error = "invalid URL";
        return false;
    }
    if (authorization.length()) http.addHeader("Authorization", authorization);
    http.addHeader("Accept", "application/json,text/html;q=0.8");

    Serial.println("HTTP GET: send");
    status = http.GET();
    Serial.printf("HTTP GET: status=%d\n", status);
    if (status <= 0) {
        error = HTTPClient::errorToString(status);
        http.end();
        return false;
    }
    if (!readHttpBody(http, body, error)) {
        http.end();
        return false;
    }
    http.end();
    return true;
}

float percentOf(uint64_t used, uint64_t total) {
    if (!total) return 0;
    return std::min(100.0f, static_cast<float>(used) * 100.0f / static_cast<float>(total));
}

float jsonFloat(JsonVariantConst value) {
    if (value.is<float>() || value.is<double>() || value.is<long>() || value.is<unsigned long>()) {
        return value.as<float>();
    }
    const char *text = value.as<const char *>();
    return text ? String(text).toFloat() : 0;
}

uint64_t jsonUint64(JsonVariantConst value) {
    if (value.is<unsigned long long>()) return value.as<unsigned long long>();
    if (value.is<long long>()) return std::max<int64_t>(0, value.as<long long>());
    const char *text = value.as<const char *>();
    return text ? strtoull(text, nullptr, 10) : 0;
}

uint64_t parseByteLimit(String text) {
    text.trim();
    if (!text.length()) return 0;
    text.toUpperCase();
    char *end = nullptr;
    const double number = strtod(text.c_str(), &end);
    if (number <= 0) return 0;
    while (end && (*end == ' ' || *end == '\t')) end++;
    uint64_t multiplier = 1;
    if (end) {
        if (*end == 'K') multiplier = 1024ULL;
        else if (*end == 'M') multiplier = 1024ULL * 1024;
        else if (*end == 'G') multiplier = 1024ULL * 1024 * 1024;
        else if (*end == 'T') multiplier = 1024ULL * 1024 * 1024 * 1024;
        else if (*end == 'P') multiplier = 1024ULL * 1024 * 1024 * 1024 * 1024;
    }
    return static_cast<uint64_t>(number * multiplier);
}

int64_t parseRfc3339(const char *value) {
    if (!value || strlen(value) < 19) return 0;
    int year, month, day, hour, minute, second;
    if (sscanf(value, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day,
               &hour, &minute, &second) != 6) {
        return 0;
    }

    // Gregorian civil date to days since 1970-01-01, independent of libc TZ.
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 +
                               static_cast<unsigned>(day) - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 +
                              dayOfYear;
    const int64_t days = static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
    int64_t epoch = days * 86400 + hour * 3600 + minute * 60 + second;

    const char *zone = value + 19;
    if (*zone == '.') {
        while (*zone && *zone != 'Z' && *zone != '+' && *zone != '-') zone++;
    }
    if (*zone == '+' || *zone == '-') {
        const int sign = *zone == '+' ? 1 : -1;
        int zoneHour = 0;
        int zoneMinute = 0;
        if (sscanf(zone + 1, "%2d:%2d", &zoneHour, &zoneMinute) >= 1) {
            epoch -= sign * (zoneHour * 3600 + zoneMinute * 60);
        }
    }
    return epoch;
}

bool isRecent(int64_t epoch, uint16_t thresholdSeconds) {
    const time_t now = time(nullptr);
    if (epoch <= 0 || now < 1700000000) return false;
    const int64_t age = static_cast<int64_t>(now) - epoch;
    return age >= -10 && age <= thresholdSeconds;
}

String formatUptime(uint64_t seconds) {
    char buffer[24];
    const uint32_t days = seconds / 86400;
    const uint32_t hours = (seconds % 86400) / 3600;
    const uint32_t minutes = (seconds % 3600) / 60;
    if (days) snprintf(buffer, sizeof(buffer), "%lud %luh", static_cast<unsigned long>(days), static_cast<unsigned long>(hours));
    else if (hours) snprintf(buffer, sizeof(buffer), "%luh %lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
    else snprintf(buffer, sizeof(buffer), "%lum", static_cast<unsigned long>(minutes));
    return buffer;
}

FetchResult fetchNezha(const AppConfig &config) {
    FetchResult result;
    String endpoint = trimBaseUrl(config.baseUrl);
    if (!endpoint.endsWith("/api/v1/server")) endpoint += "/api/v1/server";

    String auth;
    if (config.token.length()) {
        auth = config.token.startsWith("Bearer ") ? config.token : "Bearer " + config.token;
    }
    String body;
    String transportError;
    int status = 0;
    if (!httpGet(endpoint, auth, body, status, transportError)) {
        result.error = "network: " + transportError;
        return result;
    }
    if (status == 401 || status == 403) {
        result.error = "Nezha token rejected";
        return result;
    }
    if (status != 200) {
        result.error = "Nezha HTTP " + String(status);
        return result;
    }

    DynamicJsonDocument filter(4096);
    filter["success"] = true;
    filter["error"] = true;
    JsonObject item = filter["data"][0].to<JsonObject>();
    item["id"] = true;
    item["name"] = true;
    item["last_active"] = true;
    item["host"]["platform"] = true;
    item["host"]["arch"] = true;
    item["host"]["mem_total"] = true;
    item["host"]["disk_total"] = true;
    item["host"]["swap_total"] = true;
    item["host"]["virtualization"] = true;
    item["state"]["cpu"] = true;
    item["state"]["mem_used"] = true;
    item["state"]["disk_used"] = true;
    item["state"]["net_in_speed"] = true;
    item["state"]["net_out_speed"] = true;
    item["state"]["net_in_transfer"] = true;
    item["state"]["net_out_transfer"] = true;
    item["state"]["uptime"] = true;
    item["state"]["load_1"] = true;
    item["state"]["load_5"] = true;
    item["state"]["load_15"] = true;
    item["state"]["swap_used"] = true;
    item["state"]["tcp_conn_count"] = true;
    item["state"]["udp_conn_count"] = true;
    item["state"]["process_count"] = true;
    item["state"]["temperatures"][0]["Name"] = true;
    item["state"]["temperatures"][0]["Temperature"] = true;
    item["state"]["temperatures"][0]["name"] = true;
    item["state"]["temperatures"][0]["temperature"] = true;

    const size_t capacity = std::min<size_t>(384 * 1024, std::max<size_t>(32 * 1024, body.length()));
    DynamicJsonDocument doc(capacity);
    DeserializationError jsonError = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    body = "";
    if (jsonError) {
        result.error = "Nezha JSON: " + String(jsonError.c_str());
        return result;
    }
    if (!doc["success"].as<bool>()) {
        result.error = doc["error"] | "Nezha API error";
        return result;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    result.servers.reserve(std::min<size_t>(data.size(), MAX_SERVERS));
    for (JsonObjectConst server : data) {
        if (result.servers.size() >= MAX_SERVERS) break;
        ServerSnapshot snapshot;
        snapshot.id = String(server["id"].as<unsigned long long>());
        snapshot.name = server["name"] | "Unnamed";
        snapshot.platform = server["host"]["platform"] | "";
        snapshot.arch = server["host"]["arch"] | "";
        snapshot.cpuPercent = jsonFloat(server["state"]["cpu"]);
        const uint64_t memUsed = jsonUint64(server["state"]["mem_used"]);
        const uint64_t memTotal = jsonUint64(server["host"]["mem_total"]);
        snapshot.memoryPercent = percentOf(memUsed, memTotal);
        const uint64_t diskUsed = jsonUint64(server["state"]["disk_used"]);
        const uint64_t diskTotal = jsonUint64(server["host"]["disk_total"]);
        snapshot.diskPercent = percentOf(diskUsed, diskTotal);
        snapshot.netInSpeed = jsonUint64(server["state"]["net_in_speed"]);
        snapshot.netOutSpeed = jsonUint64(server["state"]["net_out_speed"]);
        snapshot.netInTransfer = jsonUint64(server["state"]["net_in_transfer"]);
        snapshot.netOutTransfer = jsonUint64(server["state"]["net_out_transfer"]);
        snapshot.uptimeSeconds = jsonUint64(server["state"]["uptime"]);
        snapshot.uptimeText = formatUptime(snapshot.uptimeSeconds);
        snapshot.load1 = jsonFloat(server["state"]["load_1"]);
        snapshot.load5 = jsonFloat(server["state"]["load_5"]);
        snapshot.load15 = jsonFloat(server["state"]["load_15"]);
        snapshot.swapPercent = percentOf(jsonUint64(server["state"]["swap_used"]),
                                         jsonUint64(server["host"]["swap_total"]));
        snapshot.processCount = jsonUint64(server["state"]["process_count"]);
        snapshot.tcpConnections = jsonUint64(server["state"]["tcp_conn_count"]);
        snapshot.udpConnections = jsonUint64(server["state"]["udp_conn_count"]);
        snapshot.virtualization = server["host"]["virtualization"] | "";
        for (JsonObjectConst sensor : server["state"]["temperatures"].as<JsonArrayConst>()) {
            const float value = sensor.containsKey("Temperature")
                                    ? jsonFloat(sensor["Temperature"])
                                    : jsonFloat(sensor["temperature"]);
            snapshot.maxTemperature = std::max(snapshot.maxTemperature, value);
        }
        snapshot.hasAdvancedMetrics = true;
        snapshot.lastActiveEpoch = parseRfc3339(server["last_active"] | "");
        snapshot.online = isRecent(snapshot.lastActiveEpoch, config.offlineSeconds);
        result.servers.push_back(std::move(snapshot));
    }

    result.success = true;
    if (result.servers.empty()) result.error = "Nezha returned no servers";
    return result;
}

void addUniqueId(std::vector<String> &ids, String id) {
    id.trim();
    if (!id.length() || ids.size() >= MAX_SERVERS) return;
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
}

std::vector<String> parseIds(String input) {
    std::vector<String> ids;
    input.replace(';', ',');
    input.replace('\n', ',');
    int start = 0;
    while (start <= input.length()) {
        int separator = input.indexOf(',', start);
        if (separator < 0) separator = input.length();
        addUniqueId(ids, input.substring(start, separator));
        start = separator + 1;
    }
    return ids;
}

std::vector<String> discoverCfIds(const AppConfig &config, const String &auth, String &error) {
    std::vector<String> ids;
    String body;
    int status = 0;
    if (!httpGet(trimBaseUrl(config.baseUrl) + "/", auth, body, status, error)) return ids;
    if (status == 401 || status == 403) {
        error = "CF password rejected";
        return ids;
    }
    if (status != 200) {
        error = "CF index HTTP " + String(status);
        return ids;
    }

    const String marker = "data-id=\"";
    int position = 0;
    while (position >= 0 && ids.size() < MAX_SERVERS) {
        position = body.indexOf(marker, position);
        if (position < 0) break;
        const int valueStart = position + marker.length();
        const int valueEnd = body.indexOf('"', valueStart);
        if (valueEnd < 0) break;
        addUniqueId(ids, body.substring(valueStart, valueEnd));
        position = valueEnd + 1;
    }
    if (ids.empty()) error = "CF IDs not found; enter them in config";
    return ids;
}

bool parseCfServer(const String &body, uint16_t offlineSeconds, ServerSnapshot &snapshot,
                   String &error) {
    DynamicJsonDocument filter(4096);
    for (const char *key : {"id", "name", "cpu", "ram", "disk", "load_avg", "uptime",
                            "last_updated", "net_rx", "net_tx", "net_in_speed", "net_out_speed",
                            "os", "arch", "swap_total", "swap_used", "processes", "tcp_conn",
                            "udp_conn", "virt", "monthly_rx", "monthly_tx", "traffic_limit",
                            "expire_date", "price", "server_group", "reset_day"}) {
        filter[key] = true;
    }
    DynamicJsonDocument doc(8192);
    DeserializationError jsonError = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (jsonError) {
        error = "CF JSON: " + String(jsonError.c_str());
        return false;
    }

    snapshot.id = doc["id"] | "";
    snapshot.name = doc["name"] | "Unnamed";
    snapshot.platform = doc["os"] | "";
    snapshot.arch = doc["arch"] | "";
    snapshot.cpuPercent = jsonFloat(doc["cpu"]);
    snapshot.memoryPercent = jsonFloat(doc["ram"]);
    snapshot.diskPercent = jsonFloat(doc["disk"]);
    snapshot.load1 = jsonFloat(doc["load_avg"]);
    snapshot.swapPercent = percentOf(jsonUint64(doc["swap_used"]), jsonUint64(doc["swap_total"]));
    snapshot.processCount = jsonUint64(doc["processes"]);
    snapshot.tcpConnections = jsonUint64(doc["tcp_conn"]);
    snapshot.udpConnections = jsonUint64(doc["udp_conn"]);
    snapshot.virtualization = doc["virt"] | "";
    snapshot.monthlyNetIn = jsonUint64(doc["monthly_rx"]);
    snapshot.monthlyNetOut = jsonUint64(doc["monthly_tx"]);
    snapshot.trafficLimitText = doc["traffic_limit"] | "";
    snapshot.trafficLimitBytes = parseByteLimit(snapshot.trafficLimitText);
    snapshot.expiryDate = doc["expire_date"] | "";
    if (snapshot.expiryDate.length() >= 10 && time(nullptr) >= 1700000000) {
        const String expiryTimestamp = snapshot.expiryDate.substring(0, 10) + "T00:00:00Z";
        const int64_t expiryEpoch = parseRfc3339(expiryTimestamp.c_str());
        if (expiryEpoch > 0) {
            snapshot.expiryDays = static_cast<int32_t>((expiryEpoch - time(nullptr)) / 86400);
        }
    }
    snapshot.price = doc["price"] | "";
    snapshot.serverGroup = doc["server_group"] | "";
    snapshot.trafficResetDay = constrain(static_cast<int>(jsonUint64(doc["reset_day"])), 0, 31);
    snapshot.hasAdvancedMetrics = true;
    snapshot.hasPlanMetrics = snapshot.monthlyNetIn || snapshot.monthlyNetOut ||
                              snapshot.trafficLimitText.length() || snapshot.expiryDate.length() ||
                              snapshot.price.length();
    snapshot.uptimeText = doc["uptime"] | "";
    snapshot.netInSpeed = jsonUint64(doc["net_in_speed"]);
    snapshot.netOutSpeed = jsonUint64(doc["net_out_speed"]);
    snapshot.netInTransfer = jsonUint64(doc["net_rx"]);
    snapshot.netOutTransfer = jsonUint64(doc["net_tx"]);
    const uint64_t updatedMs = jsonUint64(doc["last_updated"]);
    snapshot.lastActiveEpoch = updatedMs > 100000000000ULL ? updatedMs / 1000 : updatedMs;
    snapshot.online = isRecent(snapshot.lastActiveEpoch, offlineSeconds);
    return true;
}

FetchResult fetchCf(const AppConfig &config) {
    FetchResult result;
    const String base = trimBaseUrl(config.baseUrl);
    const String auth = config.token.length() ? basicAuthValue(config.token) : "";
    std::vector<String> ids = parseIds(config.cfServerIds);
    if (ids.empty()) ids = discoverCfIds(config, auth, result.error);
    if (ids.empty()) return result;

    size_t failures = 0;
    String firstError;
    for (const String &id : ids) {
        String endpoint = base;
        if (!endpoint.endsWith("/api/server")) endpoint += "/api/server";
        endpoint += "?id=" + id;
        String body;
        String transportError;
        int status = 0;
        if (!httpGet(endpoint, auth, body, status, transportError)) {
            failures++;
            if (!firstError.length()) firstError = "network: " + transportError;
            continue;
        }
        if (status == 401 || status == 403) {
            result.error = "CF password rejected";
            return result;
        }
        if (status != 200) {
            failures++;
            if (!firstError.length()) firstError = "CF " + id + " HTTP " + String(status);
            continue;
        }
        ServerSnapshot snapshot;
        String parseError;
        if (!parseCfServer(body, config.offlineSeconds, snapshot, parseError)) {
            failures++;
            if (!firstError.length()) firstError = parseError;
            continue;
        }
        result.servers.push_back(std::move(snapshot));
    }

    result.success = !result.servers.empty();
    if (failures) {
        result.error = String(failures) + " node(s) failed";
        if (firstError.length()) result.error += ": " + firstError;
    } else {
        result.error = "";
    }
    return result;
}
}  // namespace

FetchResult fetchServerSnapshots(const AppConfig &config) {
    if (WiFi.status() != WL_CONNECTED) {
        FetchResult result;
        result.error = "Wi-Fi disconnected";
        return result;
    }
    if (config.backend == BackendType::CfServerMonitor) return fetchCf(config);
    return fetchNezha(config);
}

ConnectionTestResult testServerConnection(const AppConfig &config) {
    ConnectionTestResult test;
    if (WiFi.status() != WL_CONNECTED) {
        test.stages.push_back("FAIL Wi-Fi not connected");
        return test;
    }
    test.stages.push_back("OK Wi-Fi " + WiFi.localIP().toString());

    if (!config.baseUrl.startsWith("https://") && !config.baseUrl.startsWith("http://")) {
        test.stages.push_back("FAIL URL scheme must be HTTP or HTTPS");
        return test;
    }
    test.stages.push_back(config.baseUrl.startsWith("https://") ? "OK HTTPS URL" : "WARN plaintext HTTP URL");

    FetchResult fetched = fetchServerSnapshots(config);
    if (!fetched.success) {
        if (fetched.error.startsWith("network:") || fetched.error.indexOf("disconnected") >= 0) {
            test.stages.push_back("FAIL connection/TLS: " + fetched.error);
        } else if (fetched.error.indexOf("rejected") >= 0 || fetched.error.indexOf("HTTP 401") >= 0 ||
                   fetched.error.indexOf("HTTP 403") >= 0) {
            test.stages.push_back("OK server reached");
            test.stages.push_back("FAIL authentication: " + fetched.error);
        } else if (fetched.error.indexOf("JSON") >= 0) {
            test.stages.push_back("OK HTTP response");
            test.stages.push_back("FAIL JSON parsing: " + fetched.error);
        } else {
            test.stages.push_back("FAIL API: " + fetched.error);
        }
        return test;
    }

    test.stages.push_back("OK HTTP and authentication");
    test.stages.push_back("OK JSON parsed");
    test.stages.push_back("OK nodes " + String(fetched.servers.size()));
    test.success = !fetched.servers.empty();
    return test;
}
