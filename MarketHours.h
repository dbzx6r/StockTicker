#pragma once

#include <Arduino.h>
#include <time.h>

class MarketHours {
public:
    // Configure NTP + Eastern TZ. Call after WiFi connects.
    static void begin();

    // True once the system clock has been set by NTP.
    static bool isSynced();

    // Current local time in Eastern Time (ET).
    static struct tm getLocalTime();

    // Minutes elapsed since midnight ET.
    static int minuteOfDay();

    // ─── Market-window predicates ───────────────────────────────────────────
    static bool isWeekend();
    static bool isPreMarket();      // Weekday, before 09:25
    static bool isPreOpenWarn();    // Weekday, 09:25 – 09:30
    static bool isMarketOpen();     // Weekday, 09:30 – 15:55
    static bool isPreCloseWarn();   // Weekday, 15:55 – 16:00
    static bool isAfterClose();     // After 16:00, or any weekend

    // ─── Timing helpers ────────────────────────────────────────────────────
    // True at :00, :10, :20, :30, :40, :50 — within the first 2 seconds.
    static bool isFlashMinute();

    // True at the top of each hour — within the first 2 seconds.
    static bool isHourlyMark();
};
