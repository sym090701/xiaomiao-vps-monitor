#pragma once

#include "monitor_types.h"

FetchResult fetchServerSnapshots(const AppConfig &config);
ConnectionTestResult testServerConnection(const AppConfig &config);
