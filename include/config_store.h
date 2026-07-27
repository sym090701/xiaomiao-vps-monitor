#pragma once

#include "monitor_types.h"

bool loadConfig(AppConfig &config);
bool saveConfig(const AppConfig &config);
void clearConfig();

