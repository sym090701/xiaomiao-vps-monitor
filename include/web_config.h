#pragma once

#include "monitor_types.h"

void startWebConfig(AppConfig &config);
void startConfigurationAccessPoint();
void serviceWebConfig();
bool configurationAccessPointActive();
String configurationAccessPointSsid();
String configurationAccessPointPassword();

