#pragma once

#include "monitor_types.h"

void displayInit();
void displayBoot(const String &line1, const String &line2 = "", const String &line3 = "");
void displayProvisioning(const String &ssid, const String &password, const String &ip);
void displayConnecting(const String &ssid);
void displayRefreshing();
void displayMessage(const String &title, const String &message, uint16_t color);
void displaySnapshot(const ServerSnapshot &snapshot, size_t index, size_t total,
                     uint8_t page, const String &notice = "");

