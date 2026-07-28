#include "monitor_display.h"

#include <TFT_eSPI.h>
#include <algorithm>
#include <time.h>

namespace {
TFT_eSPI tft;

String clipped(String value, size_t maxChars) {
    if (value.length() <= maxChars) return value;
    if (maxChars < 2) return value.substring(0, maxChars);
    return value.substring(0, maxChars - 1) + "~";
}

void drawHeader(const ServerSnapshot &snapshot, size_t index, size_t total) {
    tft.fillRect(0, 0, 160, 18, TFT_NAVY);
    const uint16_t statusColor = snapshot.online ? TFT_GREEN : TFT_RED;
    tft.fillCircle(7, 8, 4, statusColor);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(15, 4);
    tft.print(clipped(snapshot.name, 17));
    tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.setCursor(132, 4);
    tft.printf("%u/%u", static_cast<unsigned>(index + 1), static_cast<unsigned>(total));
}

void drawFooter(uint8_t page, const String &notice) {
    tft.fillRect(0, 116, 160, 12, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(3, 118);
    if (notice.length()) {
        tft.print(clipped(notice, 25));
    } else if (page == 0) {
        tft.print("UD:N  A:OPEN  B:REF");
    } else {
        tft.print("LR:N UD:P A:REF B:BACK");
    }
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
    tft.setTextColor(online == snapshots.size() ? TFT_GREEN : TFT_YELLOW, TFT_NAVY);
    tft.setCursor(124, 4);
    tft.printf("%u/%u", static_cast<unsigned>(online), static_cast<unsigned>(snapshots.size()));

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
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 27); tft.print("MONTH RX");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(67, 27); tft.print(formatBytes(snapshot.monthlyNetIn));
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 43); tft.print("MONTH TX");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(67, 43); tft.print(formatBytes(snapshot.monthlyNetOut));
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 59); tft.print("QUOTA");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(67, 59);
    tft.print(snapshot.trafficLimitBytes ? formatBytes(snapshot.trafficLimitBytes) : clipped(snapshot.trafficLimitText.length() ? snapshot.trafficLimitText : "--", 14));
    if (snapshot.trafficLimitBytes) {
        drawBar(76, "USE", std::min(100.0f, static_cast<float>(used) * 100.0f / snapshot.trafficLimitBytes), TFT_MAGENTA);
    }
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 94); tft.print("EXPIRE");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(54, 94); tft.print(clipped(snapshot.expiryDate.length() ? snapshot.expiryDate : "--", 17));
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(5, 107);
    if (snapshot.expiryDays != INT32_MIN) tft.printf("LEFT %ldd  RESET %u", static_cast<long>(snapshot.expiryDays), snapshot.trafficResetDay);
    else tft.printf("LEFT --  RESET %u", snapshot.trafficResetDay);
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

uint8_t displayPageCount(const ServerSnapshot &snapshot) {
    return snapshot.hasPlanMetrics ? 5 : 4;
}

void displayMonitor(const std::vector<ServerSnapshot> &snapshots, size_t index,
                    uint8_t page, const String &notice) {
    if (snapshots.empty()) return;
    if (index >= snapshots.size()) index = 0;
    const ServerSnapshot &snapshot = snapshots[index];
    const uint8_t pages = displayPageCount(snapshot);
    if (page >= pages) page = 0;
    tft.fillScreen(TFT_BLACK);
    if (page == 0) {
        drawOverview(snapshots, index);
    } else {
        drawHeader(snapshot, index, snapshots.size());
        if (page == 1) drawSummary(snapshot);
        else if (page == 2) drawNetwork(snapshot);
        else if (page == 3) drawAdvanced(snapshot);
        else drawPlan(snapshot);
    }
    drawFooter(page, notice);
}
