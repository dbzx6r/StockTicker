#pragma once

// ─── Hardware Pins (ESP8266 → MAX7219 via Hardware SPI) ─────────────────────
#define MAX_DIN_PIN  13   // D7 (MOSI)
#define MAX_CLK_PIN  14   // D5 (SCK)
#define MAX_CS_PIN   15   // D8 (CS)
#define MAX_PANELS    8   // Number of chained 8×8 panels

// MAX7219 module type — change if the display looks garbled/mirrored.
// Common options: FC16_HW (most Amazon/eBay modules), PAROLA_HW, ICSTATION_HW, GENERIC_HW
#define MAX_HARDWARE_TYPE  MD_MAX72XX::FC16_HW

// ─── Display ─────────────────────────────────────────────────────────────────
#define DISPLAY_INTENSITY   4    // 0 (dim) – 15 (bright)
#define SCROLL_SPEED_MS    50    // ms per scroll step  (lower = faster)

// ─── WiFi Setup AP ───────────────────────────────────────────────────────────
#define AP_SSID  "StockTicker-Setup"
#define AP_PASS  ""              // Leave blank for open network

// ─── NTP / Eastern Time ──────────────────────────────────────────────────────
#define NTP_SERVER  "pool.ntp.org"
#define TZ_POSIX    "EST5EDT,M3.2.0,M11.1.0"  // Auto-handles EST/EDT switchover

// ─── Market Hours (minutes from midnight, Eastern Time) ──────────────────────
#define MARKET_OPEN_MIN      570   // 09:30
#define MARKET_CLOSE_MIN     960   // 16:00
#define PRE_OPEN_WARN_MIN    565   // 09:25  (5 min before open)
#define PRE_CLOSE_WARN_MIN   955   // 15:55  (5 min before close)

// ─── Refresh Intervals ───────────────────────────────────────────────────────
#define REFRESH_MARKET_MS     60000UL   // 1 min  — during market hours
#define REFRESH_PREMARKET_MS 300000UL   // 5 min  — pre-market
#define REFRESH_AFTERHOURS_MS 600000UL  // 10 min — after hours / weekend

// ─── Display Timing ──────────────────────────────────────────────────────────
#define STOCK_CYCLE_MS   35000UL   // 35 s per stock — fits two full 15 s sub-frames + animation
#define DOW_FLASH_MS     30000UL   // 30 s DOW banner on hourly mark
#define STOCK_FLASH_MS   30000UL   // 30 s per stock on 10-min blink (after close)
#define SUBFRAME_MS      15000UL   // ms each sub-frame is held (price, then % change)
#define VERTICAL_SPEED_MS   40     // ms per step for the vertical swipe (lower = faster)

// ─── Stock Config ─────────────────────────────────────────────────────────────
#define MAX_STOCKS       5
#define MAX_TICKER_LEN  12   // Max characters in a ticker symbol (e.g. "^DJI" + null)

// ─── WiFi Retry / AP Fallback ────────────────────────────────────────────────
// After this many consecutive failed WiFi attempts, clear saved credentials and
// re-launch the setup AP so the user can re-enter them.
#define WIFI_FAIL_LIMIT  5

// ─── LittleFS ────────────────────────────────────────────────────────────────
#define CONFIG_FILE  "/config.json"
