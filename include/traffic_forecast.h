#pragma once

#include <cstdint>
#include <ctime>

struct TrafficForecast {
    bool valid = false;
    uint64_t projectedBytes = 0;
    int16_t quotaDaysLeft = -1;
    uint8_t elapsedDays = 0;
    uint8_t cycleDays = 0;
};

TrafficForecast calculateTrafficForecast(uint64_t used, uint64_t limit,
                                         uint8_t resetDay, time_t now);
