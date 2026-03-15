#include "MarketHours.h"
#include "config.h"

void MarketHours::begin() {
    // configTime sets the system clock from NTP and applies the POSIX TZ rule.
    // The rule "EST5EDT,M3.2.0,M11.1.0" automatically handles DST transitions.
    configTime(TZ_POSIX, NTP_SERVER);
}

bool MarketHours::isSynced() {
    // time() returns seconds since epoch; values > ~2001 mean NTP has responded.
    return time(nullptr) > 1000000000UL;
}

struct tm MarketHours::getLocalTime() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t;
}

int MarketHours::minuteOfDay() {
    struct tm t = getLocalTime();
    return t.tm_hour * 60 + t.tm_min;
}

bool MarketHours::isWeekend() {
    struct tm t = getLocalTime();
    return t.tm_wday == 0 || t.tm_wday == 6;  // 0 = Sunday, 6 = Saturday
}

bool MarketHours::isPreMarket() {
    return !isWeekend() && minuteOfDay() < PRE_OPEN_WARN_MIN;
}

bool MarketHours::isPreOpenWarn() {
    if (isWeekend()) return false;
    int m = minuteOfDay();
    return m >= PRE_OPEN_WARN_MIN && m < MARKET_OPEN_MIN;
}

bool MarketHours::isMarketOpen() {
    if (isWeekend()) return false;
    int m = minuteOfDay();
    return m >= MARKET_OPEN_MIN && m < PRE_CLOSE_WARN_MIN;
}

bool MarketHours::isPreCloseWarn() {
    if (isWeekend()) return false;
    int m = minuteOfDay();
    return m >= PRE_CLOSE_WARN_MIN && m < MARKET_CLOSE_MIN;
}

bool MarketHours::isAfterClose() {
    if (isWeekend()) return true;
    return minuteOfDay() >= MARKET_CLOSE_MIN;
}

bool MarketHours::isFlashMinute() {
    struct tm t = getLocalTime();
    return (t.tm_min % 10 == 0) && (t.tm_sec < 2);
}

bool MarketHours::isHourlyMark() {
    struct tm t = getLocalTime();
    return (t.tm_min == 0) && (t.tm_sec < 2);
}
