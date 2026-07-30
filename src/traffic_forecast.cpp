#include "traffic_forecast.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool leapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(int year, unsigned month) {
    static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                       31, 31, 30, 31, 30, 31};
    return month == 2 && leapYear(year) ? 29 : days[month - 1];
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

void previousMonth(int &year, unsigned &month) {
    if (--month == 0) {
        month = 12;
        year--;
    }
}

void nextMonth(int &year, unsigned &month) {
    if (++month == 13) {
        month = 1;
        year++;
    }
}
}  // namespace

TrafficForecast calculateTrafficForecast(uint64_t used, uint64_t limit,
                                         uint8_t resetDay, time_t now) {
    TrafficForecast forecast;
    if (now < 1700000000 || !used || resetDay < 1 || resetDay > 31) return forecast;

    struct tm utc {};
    if (!gmtime_r(&now, &utc)) return forecast;
    int startYear = utc.tm_year + 1900;
    unsigned startMonth = static_cast<unsigned>(utc.tm_mon + 1);
    const uint8_t thisReset = std::min(resetDay, daysInMonth(startYear, startMonth));
    if (utc.tm_mday < thisReset) previousMonth(startYear, startMonth);

    const uint8_t startDay = std::min(resetDay, daysInMonth(startYear, startMonth));
    int endYear = startYear;
    unsigned endMonth = startMonth;
    nextMonth(endYear, endMonth);
    const uint8_t endDay = std::min(resetDay, daysInMonth(endYear, endMonth));

    const int64_t start = daysFromCivil(startYear, startMonth, startDay);
    const int64_t end = daysFromCivil(endYear, endMonth, endDay);
    const int64_t today = daysFromCivil(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    const int64_t cycleDays = end - start;
    const int64_t elapsedDays = today - start + 1;
    if (cycleDays < 1 || cycleDays > 31 || elapsedDays < 1 || elapsedDays > cycleDays) return forecast;

    const long double daily = static_cast<long double>(used) / elapsedDays;
    if (!(daily > 0)) return forecast;
    const long double projected = daily * cycleDays;
    forecast.valid = true;
    forecast.elapsedDays = static_cast<uint8_t>(elapsedDays);
    forecast.cycleDays = static_cast<uint8_t>(cycleDays);
    forecast.projectedBytes = projected >= std::ldexp(1.0L, 64)
                                  ? std::numeric_limits<uint64_t>::max()
                                  : static_cast<uint64_t>(projected + 0.5L);

    if (limit) {
        if (used >= limit) {
            forecast.quotaDaysLeft = 0;
        } else {
            const long double remainingDays = std::ceil((limit - used) / daily);
            forecast.quotaDaysLeft = static_cast<int16_t>(
                std::min<long double>(remainingDays, std::numeric_limits<int16_t>::max()));
        }
    }
    return forecast;
}
