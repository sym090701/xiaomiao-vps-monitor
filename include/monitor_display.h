#pragma once

#include "monitor_types.h"

void displayInit();
void displayBoot(const String &line1, const String &line2 = "", const String &line3 = "");
void displayProvisioning(const String &ssid, const String &password, const String &ip);
void displayConnecting(const String &ssid);
void displayRefreshing();
void displayMessage(const String &title, const String &message, uint16_t color);
bool displayPageAvailable(const ServerSnapshot &snapshot, MonitorPage page);
MonitorPage displayAdjacentPage(const ServerSnapshot &snapshot, MonitorPage current,
                                int direction);
void displayMonitor(const std::vector<ServerSnapshot> &snapshots, size_t index,
                    MonitorPage page, const ServerTrends *trends,
                    const DeviceDiagnostics &diagnostics, const String &notice = "");
