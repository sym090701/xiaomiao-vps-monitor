#include "monitor_display.h"

#include <TFT_eSPI.h>
#include <algorithm>
#include <time.h>

#include "traffic_forecast.h"

namespace {
TFT_eSPI tft;

String clipped(String value, size_t maxChars) {
    if (value.length() <= maxChars) return value;
    if (maxChars < 2) return value.substring(0, maxChars);
    return value.substring(0, maxChars - 1) + "~";
}

String fitPixels(String value, int maxWidth) {
    if (tft.textWidth(value) <= maxWidth) return value;
    while (value.length() && tft.textWidth(value + "~") > maxWidth) {
        value.remove(value.length() - 1);
    }
    return value + "~";
}

void drawHeader(const ServerSnapshot &snapshot, size_t index, size_t total) {
    tft.fillRect(0, 0, 160, 18, TFT_NAVY);
    const uint16_t statusColor = snapshot.online ? TFT_GREEN : TFT_RED;
    tft.fillCircle(7, 8, 4, statusColor);
    const String counter = String(index + 1) + "/" + String(total);
    const int counterX = tft.width() - 4 - tft.textWidth(counter);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(15, 4);
    tft.print(fitPixels(snapshot.name, counterX - 19));
    tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.setCursor(counterX, 4);
    tft.print(counter);
}

void drawFooter(MonitorPage page, const String &notice) {
    tft.fillRect(0, 116, 160, 12, TFT_DARKGREY);
    tft.setTextSize(1);
    tft.setTextWrap(false);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(3, 118);
    String text;
    if (notice.length()) {
        text = notice;
    } else if (page == MonitorPage::Overview) {
        text = "UD:N <:D >:OPEN A:R B:X";
    } else if (page == MonitorPage::Diagnostics) {
        text = ">:BACK A:R B:BACK";
    } else {
        text = "LR:N UD:P A:R B:BACK";
    }
    tft.print(fitPixels(text, tft.width() - 6));
}

void drawBar(int y, const char *label, float value, uint16_t color) {
    value = constrain(value, 0.0f, 100.0f);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(4, y);
    tft.print(label);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(29, y);
    tft.printf("%5.1f%%", value);

    constexpr int x = 78;
    constexpr int width = 78;
    tft.drawRect(x, y, width, 8, TFT_DARKGREY);
    const int filled = static_cast<int>((width - 2) * value / 100.0f);
    if (filled > 0) tft.fillRect(x + 1, y + 1, filled, 6, color);
}

String formatBytes(uint64_t value, bool perSecond = false) {
    static const char *units[] = {"B", "K", "M", "G", "T"};
    float number = static_cast<float>(value);
    uint8_t unit = 0;
    while (number >= 1024.0f && unit < 4) {
        number /= 1024.0f;
        unit++;
    }
    char buffer[20];
    if (number >= 100 || unit == 0) snprintf(buffer, sizeof(buffer), "%.0f%s%s", number, units[unit], perSecond ? "/s" : "");
    else snprintf(buffer, sizeof(buffer), "%.1f%s%s", number, units[unit], perSecond ? "/s" : "");
    return buffer;
}

String ageText(int64_t epoch) {
    const time_t now = time(nullptr);
    if (epoch <= 0 || now < 1700000000) return "unknown";
    int64_t age = static_cast<int64_t>(now) - epoch;
    if (age < 0) age = 0;
    if (age < 60) return String(age) + "s ago";
    if (age < 3600) return String(age / 60) + "m ago";
    return String(age / 3600) + "h ago";
}

String durationText(uint32_t seconds) {
    if (seconds < 60) return String(seconds) + "s";
    if (seconds < 3600) return String(seconds / 60) + "m";
    if (seconds < 86400) return String(seconds / 3600) + "h " + String((seconds % 3600) / 60) + "m";
    return String(seconds / 86400) + "d " + String((seconds % 86400) / 3600) + "h";
}

void drawSummary(const ServerSnapshot &snapshot) {
    drawBar(25, "CPU", snapshot.cpuPercent, TFT_CYAN);
    drawBar(43, "RAM", snapshot.memoryPercent, TFT_GREEN);
    drawBar(61, "DSK", snapshot.diskPercent, TFT_YELLOW);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(4, 84);
    tft.print("LOAD");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(37, 84);
    tft.printf("%.2f", snapshot.load1);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(83, 84);
    tft.print("UP");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(101, 84);
    tft.print(clipped(snapshot.uptimeText, 9));

    tft.setTextColor(snapshot.online ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(4, 101);
    tft.print(snapshot.online ? "ONLINE" : "OFFLINE");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(62, 101);
    tft.print("seen ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(ageText(snapshot.lastActiveEpoch));
}

void drawNetwork(const ServerSnapshot &snapshot) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(5, 27);
    tft.print("DOWN");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(45, 23);
    tft.print(formatBytes(snapshot.netInSpeed, true));
    tft.setTextSize(1);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, 51);
    tft.print("UP");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(45, 47);
    tft.print(formatBytes(snapshot.netOutSpeed, true));
    tft.setTextSize(1);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 74);
    tft.print("TOTAL RX");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, 74);
    tft.print(formatBytes(snapshot.netInTransfer));

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 88);
    tft.print("TOTAL TX");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, 88);
    tft.print(formatBytes(snapshot.netOutTransfer));

    String host = snapshot.platform;
    if (snapshot.arch.length()) host += " / " + snapshot.arch;
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 103);
    tft.print(clipped(host.length() ? host : "Host details unavailable", 25));
}

void drawOverview(const std::vector<ServerSnapshot> &snapshots, size_t selected) {
    size_t online = 0;
    for (const auto &snapshot : snapshots) online += snapshot.online ? 1 : 0;

    tft.fillRect(0, 0, 160, 18, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(5, 4);
    tft.print("FLEET OVERVIEW");
    const String counter = String(online) + "/" + String(snapshots.size());
    tft.setTextColor(online == snapshots.size() ? TFT_GREEN : TFT_YELLOW, TFT_NAVY);
    tft.setCursor(tft.width() - 4 - tft.textWidth(counter), 4);
    tft.print(counter);

    const size_t visible = std::min<size_t>(5, snapshots.size());
    size_t first = 0;
    if (snapshots.size() > visible && selected >= visible) first = selected - visible + 1;
    for (size_t row = 0; row < visible; row++) {
        const size_t nodeIndex = first + row;
        const ServerSnapshot &node = snapshots[nodeIndex];
        const int y = 24 + row * 17;
        const bool isSelected = nodeIndex == selected;
        const uint16_t background = isSelected ? TFT_DARKGREY : TFT_BLACK;
        if (isSelected) tft.fillRect(0, y - 2, 160, 13, background);
        tft.setTextColor(TFT_CYAN, background);
        tft.setCursor(4, y);
        tft.print(isSelected ? ">" : " ");
        tft.setTextColor(node.online ? TFT_GREEN : TFT_RED, background);
        tft.setCursor(12, y);
        tft.print(node.online ? "+" : "!");
        tft.setTextColor(TFT_WHITE, background);
        tft.setCursor(22, y);
        tft.print(clipped(node.name, 12));
        tft.setTextColor(TFT_LIGHTGREY, background);
        tft.setCursor(99, y);
        if (node.online) {
            const float worst = std::max(node.cpuPercent, std::max(node.memoryPercent, node.diskPercent));
            tft.printf("%3.0f%%", worst);
        } else {
            tft.print("OFF");
        }
    }
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(4, 108);
    tft.printf("SELECT %u/%u", static_cast<unsigned>(selected + 1), static_cast<unsigned>(snapshots.size()));
}

void drawAdvanced(const ServerSnapshot &snapshot) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 27); tft.print("LOAD 1/5/15");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 40); tft.printf("%.2f  %.2f  %.2f", snapshot.load1, snapshot.load5, snapshot.load15);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 58); tft.print("SWAP");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(43, 58); tft.printf("%.1f%%", snapshot.swapPercent);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(92, 58); tft.print("TEMP");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(126, 58);
    if (snapshot.maxTemperature > 0) tft.printf("%.0fC", snapshot.maxTemperature);
    else tft.print("--");

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 76); tft.print("PROC");
    tft.setCursor(62, 76); tft.print("TCP");
    tft.setCursor(112, 76); tft.print("UDP");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 89); tft.print(snapshot.processCount);
    tft.setCursor(62, 89); tft.print(snapshot.tcpConnections);
    tft.setCursor(112, 89); tft.print(snapshot.udpConnections);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 105); tft.print("VIRT ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(clipped(snapshot.virtualization.length() ? snapshot.virtualization : "--", 18));
}

void drawPlan(const ServerSnapshot &snapshot) {
    const uint64_t used = snapshot.monthlyNetIn + snapshot.monthlyNetOut;
    const TrafficForecast forecast = calculateTrafficForecast(
        used, snapshot.trafficLimitBytes, snapshot.trafficResetDay, time(nullptr));
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 24); tft.print("USED");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(37, 24); tft.print(formatBytes(used));
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.print(" / ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(snapshot.trafficLimitBytes ? formatBytes(snapshot.trafficLimitBytes)
                                        : clipped(snapshot.trafficLimitText.length() ? snapshot.trafficLimitText : "--", 10));
    if (snapshot.trafficLimitBytes) {
        drawBar(39, "USE", std::min(100.0f, static_cast<float>(used) * 100.0f /
                                             snapshot.trafficLimitBytes), TFT_MAGENTA);
    }
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 57); tft.print("PROJECT");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(58, 57);
    tft.print(forecast.valid ? formatBytes(forecast.projectedBytes) : "--");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 72); tft.print("QUOTA LEFT");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(72, 72);
    if (!forecast.valid || forecast.quotaDaysLeft < 0) tft.print("--");
    else tft.printf("%dd", forecast.quotaDaysLeft);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 87); tft.print("CYCLE");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(42, 87);
    if (forecast.valid) tft.printf("%u/%ud", forecast.elapsedDays, forecast.cycleDays);
    else tft.print("--");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(96, 87); tft.print("RESET");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(132, 87); tft.print(snapshot.trafficResetDay ? String(snapshot.trafficResetDay) : "--");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 102); tft.print("EXPIRE LEFT");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(78, 102);
    if (snapshot.expiryDays != INT32_MIN) tft.printf("%ldd", static_cast<long>(snapshot.expiryDays));
    else tft.print("--");
}

uint8_t trendIndex(const ServerTrends &trends, uint8_t position) {
    const uint8_t first = trends.count == TREND_SAMPLE_COUNT ? trends.next : 0;
    return (first + position) % TREND_SAMPLE_COUNT;
}

int chartY(float value, float scale, int top, int height) {
    value = constrain(value, 0.0f, scale);
    return top + height - 2 - static_cast<int>(value * (height - 3) / scale);
}

void drawTrends(const ServerTrends *trends) {
    if (!trends || trends->count < 2) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(18, 53);
        tft.print("COLLECTING SAMPLES");
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.setCursor(47, 71);
        tft.printf("%u / %u ready", trends ? trends->count : 0, TREND_SAMPLE_COUNT);
        return;
    }

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, 21); tft.print("CPU");
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(31, 21); tft.print("RAM");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(113, 21); tft.printf("%u pts", trends->count);
    constexpr int chartX = 4;
    constexpr int chartWidth = 152;
    constexpr int resourceTop = 31;
    constexpr int resourceHeight = 32;
    tft.drawRect(chartX, resourceTop, chartWidth, resourceHeight, TFT_DARKGREY);
    tft.drawFastHLine(chartX + 1, resourceTop + resourceHeight / 2,
                      chartWidth - 2, TFT_DARKGREY);

    uint64_t networkPeak = 1;
    for (uint8_t i = 0; i < trends->count; i++) {
        networkPeak = std::max(networkPeak, trends->network[trendIndex(*trends, i)]);
    }
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(5, 68); tft.print("NET");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(70, 68); tft.print("PEAK ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(formatBytes(networkPeak, true));
    constexpr int networkTop = 79;
    constexpr int networkHeight = 32;
    tft.drawRect(chartX, networkTop, chartWidth, networkHeight, TFT_DARKGREY);

    for (uint8_t i = 1; i < trends->count; i++) {
        const uint8_t previous = trendIndex(*trends, i - 1);
        const uint8_t current = trendIndex(*trends, i);
        const int x1 = chartX + 1 + (i - 1) * (chartWidth - 3) / (trends->count - 1);
        const int x2 = chartX + 1 + i * (chartWidth - 3) / (trends->count - 1);
        tft.drawLine(x1, chartY(trends->cpu[previous], 100.0f, resourceTop, resourceHeight),
                     x2, chartY(trends->cpu[current], 100.0f, resourceTop, resourceHeight), TFT_CYAN);
        tft.drawLine(x1, chartY(trends->memory[previous], 100.0f, resourceTop, resourceHeight),
                     x2, chartY(trends->memory[current], 100.0f, resourceTop, resourceHeight), TFT_GREEN);
        tft.drawLine(x1, chartY(static_cast<float>(trends->network[previous]),
                                static_cast<float>(networkPeak), networkTop, networkHeight),
                     x2, chartY(static_cast<float>(trends->network[current]),
                                static_cast<float>(networkPeak), networkTop, networkHeight), TFT_YELLOW);
    }
}

void drawDiagnostics(const DeviceDiagnostics &diagnostics) {
    tft.fillRect(0, 0, 160, 18, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(5, 4);
    tft.print("DEVICE DIAGNOSTICS");

    auto line = [](int y, const char *label, const String &value, uint16_t color = TFT_WHITE) {
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.setCursor(5, y); tft.print(label);
        tft.setTextColor(color, TFT_BLACK);
        tft.setCursor(54, y); tft.print(clipped(value, 17));
    };
    line(22, "FW", FIRMWARE_VERSION, TFT_CYAN);
    line(34, "WIFI", diagnostics.wifiConnected
                         ? "OK " + String(diagnostics.wifiRssi) + "dBm" : "OFFLINE",
         diagnostics.wifiConnected ? TFT_GREEN : TFT_RED);
    line(46, "IP", diagnostics.localIp.length() ? diagnostics.localIp : "--");
    line(58, "REQUEST", String(diagnostics.requestDurationMs) + "ms");
    line(70, "REFRESH", diagnostics.lastRefreshAgeSeconds == UINT32_MAX
                            ? "--" : durationText(diagnostics.lastRefreshAgeSeconds) + " ago");
    line(82, "HEAP", formatBytes(diagnostics.freeHeap));
    line(94, "PSRAM", formatBytes(diagnostics.freePsram));
    line(106, "UPTIME", durationText(diagnostics.deviceUptimeSeconds));
}
}  // namespace

void displayInit() {
    tft.init();
    tft.setRotation(3);
    tft.setTextWrap(false);
    tft.setTextSize(1);
    tft.fillScreen(TFT_BLACK);
}

void displayBoot(const String &line1, const String &line2, const String &line3) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 160, 22, TFT_NAVY);
    tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.setTextSize(2);
    tft.setCursor(10, 3);
    tft.print("VPS MONITOR");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(8, 39);
    tft.print(clipped(line1, 24));
    tft.setCursor(8, 59);
    tft.print(clipped(line2, 24));
    tft.setCursor(8, 79);
    tft.print(clipped(line3, 24));
}

void displayProvisioning(const String &ssid, const String &password, const String &ip) {
    displayBoot("CONFIG ACCESS POINT", "Wi-Fi: " + ssid, "Pass: " + password);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(8, 99);
    tft.print("Open http://" + ip);
}

void displayConnecting(const String &ssid) {
    displayBoot("Connecting Wi-Fi", clipped(ssid, 23), "Please wait...");
}

void displayRefreshing() {
    tft.fillRect(0, 116, 160, 12, TFT_DARKGREY);
    tft.setTextColor(TFT_YELLOW, TFT_DARKGREY);
    tft.setCursor(4, 118);
    tft.print("Refreshing server data...");
}

void displayMessage(const String &title, const String &message, uint16_t color) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 160, 20, color);
    tft.setTextColor(TFT_WHITE, color);
    tft.setCursor(5, 6);
    tft.print(clipped(title, 25));
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 30);
    tft.setTextWrap(true);
    tft.print(message);
    tft.setTextWrap(false);
}

bool displayPageAvailable(const ServerSnapshot &snapshot, MonitorPage page) {
    return page != MonitorPage::Plan || snapshot.hasPlanMetrics;
}

MonitorPage displayAdjacentPage(const ServerSnapshot &snapshot, MonitorPage current,
                                int direction) {
    constexpr int first = static_cast<int>(MonitorPage::Summary);
    constexpr int last = static_cast<int>(MonitorPage::Trends);
    const int step = direction < 0 ? -1 : 1;
    int candidate = static_cast<int>(current);
    for (int attempts = 0; attempts <= last - first; attempts++) {
        candidate += step;
        if (candidate < first) candidate = last;
        if (candidate > last) candidate = first;
        const auto page = static_cast<MonitorPage>(candidate);
        if (displayPageAvailable(snapshot, page)) return page;
    }
    return current;
}

void displayMonitor(const std::vector<ServerSnapshot> &snapshots, size_t index,
                    MonitorPage page, const ServerTrends *trends,
                    const DeviceDiagnostics &diagnostics, const String &notice) {
    if (snapshots.empty()) return;
    if (index >= snapshots.size()) index = 0;
    const ServerSnapshot &snapshot = snapshots[index];
    if (!displayPageAvailable(snapshot, page)) page = MonitorPage::Advanced;
    tft.fillScreen(TFT_BLACK);
    if (page == MonitorPage::Overview) {
        drawOverview(snapshots, index);
    } else if (page == MonitorPage::Diagnostics) {
        drawDiagnostics(diagnostics);
    } else {
        drawHeader(snapshot, index, snapshots.size());
        if (page == MonitorPage::Summary) drawSummary(snapshot);
        else if (page == MonitorPage::Network) drawNetwork(snapshot);
        else if (page == MonitorPage::Advanced) drawAdvanced(snapshot);
        else if (page == MonitorPage::Plan) drawPlan(snapshot);
        else if (page == MonitorPage::Trends) drawTrends(trends);
    }
    drawFooter(page, notice);
}
