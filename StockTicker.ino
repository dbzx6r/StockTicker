/*
 * StockTicker.ino — ESP8266 + MAX7219 Stock Ticker
 *
 * Displays real-time stock prices on an 8-panel MAX7219 LED matrix.
 * Setup is performed via a captive-portal WiFi hotspot ("StockTicker-Setup").
 *
 * Required libraries (install via Arduino Library Manager):
 *   MD_Parola, MD_MAX72XX, ArduinoJson (v6), ESP8266 core
 *
 * Board: "NodeMCU 1.0 (ESP-12E Module)" or any ESP8266 board.
 *
 * NOTE: The sketch folder MUST be named "StockTicker" so that Arduino IDE
 *       recognises it. Rename the folder if needed before opening.
 */

// ─── State Machine ────────────────────────────────────────────────────────
// Defined before includes so Arduino IDE's auto-generated function prototypes
// (inserted after the #include block) can reference AppState.
enum AppState {
    STATE_SETUP_AP,
    STATE_CONNECTING,
    STATE_NTP_SYNC,
    STATE_PRE_MARKET,
    STATE_PRE_OPEN_WARN,
    STATE_MARKET_OPEN,
    STATE_PRE_CLOSE_WARN,
    STATE_AFTER_CLOSE
};

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "DisplayManager.h"
#include "WiFiSetup.h"
#include "StockAPI.h"
#include "Storage.h"
#include "MarketHours.h"

// ─── RTC memory: persist state across reboots ─────────────────────────────
// RTC user memory survives ESP.restart() but is cleared on power cycle.
// NOTE: RTC_MAGIC was bumped to 0xBEEFCAFFu when drdFlag was added so that
//       boards running the old two-field layout get a clean re-initialisation.
struct RtcData {
    uint32_t magic;
    uint32_t wifiFailCount;
    uint32_t drdFlag;   // double-reset detector: 1 = armed, 0 = cleared
};
static const uint32_t RTC_MAGIC  = 0xBEEFCAFFu;
static const uint8_t  RTC_OFFSET = 0;   // offset in 4-byte blocks

static RtcData rtc;

static void rtcLoad() {
    if (!ESP.rtcUserMemoryRead(RTC_OFFSET, (uint32_t*)&rtc, sizeof(rtc))
            || rtc.magic != RTC_MAGIC) {
        rtc.magic         = RTC_MAGIC;
        rtc.wifiFailCount = 0;
        rtc.drdFlag       = 0;
    }
}
static void rtcSave() {
    ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&rtc, sizeof(rtc));
}

// ─── Globals ──────────────────────────────────────────────────────────────
static AppState  state       = STATE_SETUP_AP;
static AppConfig cfg;

// stockData[0..cfg.stockCount-1] = user stocks; stockData[cfg.stockCount] = DOW
static StockData     stockData[MAX_STOCKS + 1];
static const char*   querySymbols[MAX_STOCKS + 1];
static int           totalSymbols = 0;

static unsigned long lastFetchMs      = 0;
static unsigned long lastDisplayedFetch = 0;  // Tracks when display last reflected data
static unsigned long stateStartMs     = 0;
static unsigned long cycleStartMs     = 0;

static int  stockIdx     = 0;   // Current stock slot being shown
static bool inDowFlash   = false;
static bool inStockFlash = false;

// Sub-frame: alternates price (0) and % change (1) within each stock slot
static int           subframeIdx     = 0;

// ── 3-phase vertical animation state ─────────────────────────────────────────
// VP_ENTERING : swipe-in animation playing
// VP_SHOWING  : text static on screen; timer running
// VP_EXITING  : swipe-out animation playing
enum VertPhase { VP_ENTERING, VP_SHOWING, VP_EXITING };
static VertPhase     vertPhase           = VP_ENTERING;
static unsigned long showStartMs         = 0;     // when VP_SHOWING began
static bool          pendingStockAdvance = false; // stock-cycle fired while showing

// Edge-detect helpers so each hourly / 10-min event fires exactly once
static int  lastHourMark  = -1;
static int  lastFlashMin  = -1;

// ─── DOW convenience reference ────────────────────────────────────────────
static inline StockData& dow() { return stockData[cfg.stockCount]; }

// ─── Build the symbol list sent to Yahoo Finance ─────────────────────────
static void buildQueryList() {
    totalSymbols = 0;
    for (int i = 0; i < cfg.stockCount; i++)
        querySymbols[totalSymbols++] = cfg.stocks[i];
    querySymbols[totalSymbols++] = "^DJI";
}

// ─── Data fetch ───────────────────────────────────────────────────────────
static void fetchData() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[main] WiFi disconnected — attempting reconnect"));
        WiFi.reconnect();
        return;
    }
    Serial.println(F("[main] Fetching quotes…"));
    StockAPI::fetchQuotes(querySymbols, totalSymbols, stockData);
    lastFetchMs = millis();
}

// ─── Display helpers ──────────────────────────────────────────────────────

// "DOW +0.42%" — change vs. previous close
static void showDowVsPrevClose() {
    if (!dow().valid) { DisplayManager::staticText("DOW ---"); return; }
    char buf[32];
    char sign = dow().changePct >= 0 ? '+' : '-';
    snprintf(buf, sizeof(buf), "DOW %c%.2f%%", sign, fabsf(dow().changePct));
    DisplayManager::staticText(buf);
}


// "AAPL $173.42"  — swiped in vertically during cycling
static void showPrice(int idx) {
    if (idx >= cfg.stockCount || !stockData[idx].valid) {
        DisplayManager::verticalEnter("---");
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%s $%.2f",
             stockData[idx].symbol, stockData[idx].price);
    DisplayManager::verticalEnter(buf);
}

// "AAPL +2.31%"  — change vs. previous close (matches Yahoo's headline metric)
static void showChange(int idx) {
    if (idx >= cfg.stockCount || !stockData[idx].valid) {
        DisplayManager::verticalEnter("---");
        return;
    }
    StockData& s = stockData[idx];
    float  pct   = s.changePct;
    char   sign  = pct >= 0 ? '+' : '-';
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %c%.2f%%", s.symbol, sign, fabsf(pct));
    DisplayManager::verticalEnter(buf);
}

// "DOW +0.42%"  — used in cycling contexts (vertically animated)
static void showDowCycled() {
    if (!dow().valid) { DisplayManager::verticalEnter("DOW ---"); return; }
    char buf[32];
    char sign = dow().changePct >= 0 ? '+' : '-';
    snprintf(buf, sizeof(buf), "DOW %c%.2f%%", sign, fabsf(dow().changePct));
    DisplayManager::verticalEnter(buf);
}

// Show the current sub-frame (price or % change) for a stock slot
static void showSubframe(int idx) {
    if (subframeIdx == 0) showPrice(idx);
    else                  showChange(idx);
}

// Flash (blink) a stock price for attention
static void flashPrice(int idx) {
    if (idx >= cfg.stockCount || !stockData[idx].valid) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s $%.2f",
             stockData[idx].symbol, stockData[idx].price);
    DisplayManager::flashText(buf);
}

// ─── State transitions ────────────────────────────────────────────────────
static AppState resolveMarketState() {
    if (MarketHours::isPreOpenWarn())  return STATE_PRE_OPEN_WARN;
    if (MarketHours::isMarketOpen())   return STATE_MARKET_OPEN;
    if (MarketHours::isPreCloseWarn()) return STATE_PRE_CLOSE_WARN;
    if (MarketHours::isAfterClose())   return STATE_AFTER_CLOSE;
    return STATE_PRE_MARKET;
}

static void enterState(AppState next) {
    Serial.printf("[main] → State %d\n", (int)next);
    state        = next;
    stateStartMs = millis();
    cycleStartMs = millis();
    stockIdx            = 0;
    inDowFlash          = false;
    inStockFlash        = false;
    subframeIdx         = 0;
    vertPhase           = VP_ENTERING;
    showStartMs         = 0;
    pendingStockAdvance = false;

    switch (next) {
        case STATE_PRE_MARKET:
            showDowVsPrevClose();
            break;
        case STATE_PRE_OPEN_WARN:
            DisplayManager::scrollText("5 MINUTES UNTIL MARKET OPEN", true);
            break;
        case STATE_MARKET_OPEN:
            showSubframe(0);
            break;
        case STATE_PRE_CLOSE_WARN:
            DisplayManager::scrollText("5 MINUTES UNTIL MARKET CLOSE", true);
            break;
        case STATE_AFTER_CLOSE:
            showSubframe(0);
            break;
        default:
            break;
    }
}

// ─── setup() ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n\n[StockTicker] Booting…"));

    // Mount filesystem — required for config persistence
    if (!Storage::begin()) {
        // First attempt: try reformatting (recovers from flash corruption)
        Serial.println(F("[main] LittleFS mount failed — attempting reformat"));
        LittleFS.format();
        if (!Storage::begin()) {
            DisplayManager::begin();
            DisplayManager::staticText("FS ERR");
            Serial.println(F("[main] LittleFS unrecoverable — halting"));
            while (true) delay(1000);
        }
        Serial.println(F("[main] LittleFS reformatted OK — entering setup"));
        // After format no config exists, falls through to setup-AP path below
    }

    DisplayManager::begin();
    DisplayManager::scrollText("LOADING...", false);

    // ── Double-reset detection ──────────────────────────────────────────────
    // Arm the flag on every boot. If the board resets again within the 3-second
    // window (user held / tapped the reset button), the next boot sees drdFlag=1
    // and launches the setup portal so the user can change WiFi or stocks.
    rtcLoad();
    bool doubleReset = (rtc.drdFlag == 1);
    rtc.drdFlag = 1;
    rtcSave();

    {   // 3-second detection window — keep the display ticking
        unsigned long t = millis();
        while (millis() - t < 3000UL)
            DisplayManager::update();
    }
    rtc.drdFlag = 0;
    rtcSave();

    if (doubleReset) {
        Serial.println(F("[main] Double-reset detected — launching setup AP"));
        Storage::loadConfig(cfg);   // pre-load existing values (best-effort)
        DisplayManager::scrollText("SETUP - CONNECT TO StockTicker-Setup", true);
        WiFiSetup::begin(cfg);
        state = STATE_SETUP_AP;
        return;
    }

    if (!Storage::hasConfig()) {
        // ── First-boot: launch captive portal ──────────────────────────
        Serial.println(F("[main] No config — starting setup AP"));
        DisplayManager::scrollText("CONNECT TO StockTicker-Setup", true);
        WiFiSetup::begin(cfg);
        state = STATE_SETUP_AP;
        return;  // loop() will call WiFiSetup::handle() until reboot
    }

    // ── Normal boot ────────────────────────────────────────────────────
    if (!Storage::loadConfig(cfg)) {
        Serial.println(F("[main] Config corrupt — clearing and rebooting"));
        Storage::clearConfig();
        delay(1000);
        ESP.restart();
    }

    buildQueryList();

    // Connect to WiFi
    state = STATE_CONNECTING;
    Serial.printf("[main] Connecting to SSID: \"%s\"\n", cfg.ssid);
    DisplayManager::scrollText("CONNECTING...", true);
    WiFi.mode(WIFI_STA);
    // Reliability settings — must be set before WiFi.begin()
    WiFi.persistent(false);          // Don't wear out flash writing credentials
    WiFi.setSleepMode(WIFI_NONE_SLEEP); // Prevent modem sleep dropping the connection
    WiFi.setAutoReconnect(false);    // We manage reconnects manually
    WiFi.disconnect(true);           // Clear any stale connection state
    delay(100);

    WiFi.begin(cfg.ssid, cfg.password);

    unsigned long t0 = millis();
    wl_status_t   lastStatus = WL_IDLE_STATUS;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000UL) {
        DisplayManager::update();
        wl_status_t s = WiFi.status();
        if (s != lastStatus) {
            lastStatus = s;
            switch (s) {
                case WL_IDLE_STATUS:     Serial.println(F("[wifi] Status: IDLE"));              break;
                case WL_NO_SSID_AVAIL:   Serial.println(F("[wifi] Status: SSID NOT FOUND"));   break;
                case WL_SCAN_COMPLETED:  Serial.println(F("[wifi] Status: SCAN COMPLETED"));   break;
                case WL_CONNECT_FAILED:  Serial.println(F("[wifi] Status: CONNECT FAILED (wrong password?)"));  break;
                case WL_CONNECTION_LOST: Serial.println(F("[wifi] Status: CONNECTION LOST"));  break;
                case WL_DISCONNECTED:    Serial.println(F("[wifi] Status: DISCONNECTED"));     break;
                default:                 Serial.printf( "[wifi] Status: %d\n", (int)s);        break;
            }
        }
        delay(200);
    }

    if (WiFi.status() != WL_CONNECTED) {
        rtc.wifiFailCount++;
        rtcSave();
        Serial.printf("[main] WiFi failed (attempt %u/%u)\n",
                      rtc.wifiFailCount, (unsigned)WIFI_FAIL_LIMIT);

        if (rtc.wifiFailCount >= WIFI_FAIL_LIMIT) {
            // Credentials are likely wrong — wipe them and relaunch setup portal
            Serial.println(F("[main] Too many WiFi failures — clearing config, launching setup AP"));
            rtc.wifiFailCount = 0;
            rtcSave();
            Storage::clearConfig();
            DisplayManager::scrollText("BAD WIFI - RECONNECT TO StockTicker-Setup", true);
            WiFiSetup::begin(cfg);
            state = STATE_SETUP_AP;
            return;
        }

        DisplayManager::staticText("NO WIFI");
        delay(3000);
        ESP.restart();
    }

    // Successful connection — reset the fail counter
    rtc.wifiFailCount = 0;
    rtcSave();
    Serial.printf("[main] WiFi connected: %s\n", WiFi.localIP().toString().c_str());

    // NTP sync
    state = STATE_NTP_SYNC;
    DisplayManager::scrollText("SYNCING...", true);
    MarketHours::begin();

    t0 = millis();
    while (!MarketHours::isSynced() && millis() - t0 < 20000UL) {
        DisplayManager::update();
        delay(200);
    }

    if (!MarketHours::isSynced()) {
        Serial.println(F("[main] NTP timeout — will retry in background"));
    }

    // Initial data fetch then enter the correct market state
    fetchData();
    enterState(resolveMarketState());
}

// ─── loop() ───────────────────────────────────────────────────────────────
void loop() {
    DisplayManager::update();

    // ── Setup-AP mode: hand off entirely to the portal ────────────────────
    if (state == STATE_SETUP_AP) {
        WiFiSetup::handle();
        return;
    }

    // ── Wait for NTP sync before driving the state machine ────────────────
    if (!MarketHours::isSynced()) return;

    unsigned long now = millis();

    // ── Periodic WiFi health check (every 15 s) ───────────────────────────
    static unsigned long lastWifiCheckMs = 0;
    if (now - lastWifiCheckMs >= 15000UL) {
        lastWifiCheckMs = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[main] WiFi lost — reconnecting"));
            WiFi.reconnect();
        }
    }

    // ── Periodic data refresh ─────────────────────────────────────────────
    unsigned long refreshInterval = REFRESH_AFTERHOURS_MS;
    if (MarketHours::isMarketOpen()    ||
        MarketHours::isPreOpenWarn()   ||
        MarketHours::isPreCloseWarn())   refreshInterval = REFRESH_MARKET_MS;
    else if (MarketHours::isPreMarket()) refreshInterval = REFRESH_PREMARKET_MS;

    if (now - lastFetchMs >= refreshInterval) fetchData();

    // ── State transition check ────────────────────────────────────────────
    AppState desired = resolveMarketState();
    if (desired != state) {
        enterState(desired);
        return;
    }

    // ── State-specific behaviour ──────────────────────────────────────────
    switch (state) {

        // ── PRE_MARKET: show DOW vs. prev close; refresh when data updates ─
        case STATE_PRE_MARKET:
            if (lastFetchMs > lastDisplayedFetch) {
                lastDisplayedFetch = lastFetchMs;
                showDowVsPrevClose();
            }
            break;

        // ── MARKET_OPEN ────────────────────────────────────────────────────
        case STATE_MARKET_OPEN: {
            struct tm t = MarketHours::getLocalTime();

            // Hourly DOW flash — edge-detected by hour value
            if (MarketHours::isHourlyMark() &&
                t.tm_hour != lastHourMark   &&
                !inDowFlash) {
                lastHourMark = t.tm_hour;
                inDowFlash   = true;
                stateStartMs = now;
                showDowVsPrevClose();
                Serial.println(F("[main] Hourly DOW flash"));
            }

            if (inDowFlash) {
                if (now - stateStartMs >= DOW_FLASH_MS) {
                    inDowFlash          = false;
                    cycleStartMs        = now;
                    subframeIdx         = 0;
                    vertPhase           = VP_ENTERING;
                    showStartMs         = 0;
                    pendingStockAdvance = false;
                    showSubframe(stockIdx);
                }
                break;
            }

            // ── 3-phase vertical cycling ──────────────────────────────────
            if (vertPhase == VP_ENTERING && DisplayManager::isEnterDone()) {
                vertPhase   = VP_SHOWING;
                showStartMs = now;
                // Mark any pending data refresh as displayed
                lastDisplayedFetch = lastFetchMs;
            }

            if (vertPhase == VP_SHOWING) {
                bool stockCycle = (now - cycleStartMs >= STOCK_CYCLE_MS);
                bool subCycle   = !stockCycle && (now - showStartMs >= SUBFRAME_MS);

                if (stockCycle || subCycle) {
                    pendingStockAdvance = stockCycle;
                    vertPhase = VP_EXITING;
                    DisplayManager::verticalExit();
                }
            }

            if (vertPhase == VP_EXITING && DisplayManager::isExitDone()) {
                if (pendingStockAdvance) {
                    stockIdx            = (stockIdx + 1) % cfg.stockCount;
                    cycleStartMs        = now;
                    subframeIdx         = 0;
                    pendingStockAdvance = false;
                } else {
                    subframeIdx = 1 - subframeIdx;
                }
                vertPhase   = VP_ENTERING;
                showStartMs = 0;
                showSubframe(stockIdx);
            }
            break;
        }

        // ── AFTER_CLOSE ────────────────────────────────────────────────────
        case STATE_AFTER_CLOSE: {
            struct tm t        = MarketHours::getLocalTime();
            int       flashKey = t.tm_hour * 6 + t.tm_min / 10;

            // 10-min flash — edge-detected by (hour * 6 + min/10)
            if (MarketHours::isFlashMinute() &&
                flashKey != lastFlashMin      &&
                !inStockFlash) {
                lastFlashMin = flashKey;
                inStockFlash = true;
                stockIdx     = 0;
                stateStartMs = now;
                flashPrice(0);
                Serial.println(F("[main] 10-min stock flash"));
            }

            if (inStockFlash) {
                if (now - stateStartMs >= STOCK_FLASH_MS) {
                    stockIdx++;
                    if (stockIdx >= cfg.stockCount) {
                        // All stocks flashed — return to sub-frame cycling
                        inStockFlash        = false;
                        stockIdx            = 0;
                        cycleStartMs        = now;
                        subframeIdx         = 0;
                        vertPhase           = VP_ENTERING;
                        showStartMs         = 0;
                        pendingStockAdvance = false;
                        showSubframe(0);
                    } else {
                        stateStartMs = now;
                        flashPrice(stockIdx);
                    }
                }
                break;
            }

            // ── 3-phase vertical cycling (stocks + DOW slot) ──────────────
            // total slots: cfg.stockCount stocks + 1 DOW
            int total = cfg.stockCount + 1;

            if (vertPhase == VP_ENTERING && DisplayManager::isEnterDone()) {
                vertPhase          = VP_SHOWING;
                showStartMs        = now;
                lastDisplayedFetch = lastFetchMs;
            }

            if (vertPhase == VP_SHOWING) {
                bool stockCycle = (now - cycleStartMs >= STOCK_CYCLE_MS);
                // DOW slot has no alternate subframe — only cycle on stock timer
                bool subCycle   = !stockCycle
                                  && (stockIdx < cfg.stockCount)
                                  && (now - showStartMs >= SUBFRAME_MS);

                if (stockCycle || subCycle) {
                    pendingStockAdvance = stockCycle;
                    vertPhase = VP_EXITING;
                    DisplayManager::verticalExit();
                }
            }

            if (vertPhase == VP_EXITING && DisplayManager::isExitDone()) {
                if (pendingStockAdvance) {
                    stockIdx            = (stockIdx + 1) % total;
                    cycleStartMs        = now;
                    subframeIdx         = 0;
                    pendingStockAdvance = false;
                } else {
                    subframeIdx = 1 - subframeIdx;   // only reachable for stock slots
                }
                vertPhase   = VP_ENTERING;
                showStartMs = 0;
                if (stockIdx < cfg.stockCount) showSubframe(stockIdx);
                else                           showDowCycled();
            }
            break;
        }

        default:
            break;
    }
}
