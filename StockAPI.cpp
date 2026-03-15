#include "StockAPI.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>

// Fetches a single symbol via the v8 chart endpoint (no auth required).
// Parses: price, open, prevClose, change, changePct from the chart meta block.
static bool fetchOne(const char* sym, StockData& s) {
    String url = F("https://query2.finance.yahoo.com/v8/finance/chart/");
    // URL-encode '^' for index symbols like ^DJI
    for (const char* p = sym; *p; p++) {
        if (*p == '^') url += F("%5E");
        else           url += *p;
    }
    url += F("?interval=1d&range=1d");

    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    HTTPClient https;
    https.setTimeout(15000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    https.addHeader(F("User-Agent"),
        F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
          "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    https.addHeader(F("Accept"), F("application/json"));

    if (!https.begin(client, url)) {
        Serial.printf("[StockAPI] begin() failed for %s\n", sym);
        return false;
    }

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[StockAPI] %s → HTTP %d\n", sym, code);
        https.end();
        return false;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, https.getStream());
    https.end();

    if (err) {
        Serial.printf("[StockAPI] %s JSON error: %s\n", sym, err.c_str());
        return false;
    }

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) {
        Serial.printf("[StockAPI] %s — no meta block\n", sym);
        return false;
    }

    strlcpy(s.symbol, sym, MAX_TICKER_LEN);
    s.price = meta["regularMarketPrice"] | 0.0f;

    // Yahoo Finance computes "change from open" against regularMarketOpen (the
    // official 9:30 AM bell price).  The indicators.quote[0].open[0] candle
    // can include pre-market activity and won't match.  Use regularMarketOpen
    // as the primary source, fall back to the candle, then to prevClose.
    s.open = meta["regularMarketOpen"] | 0.0f;
    if (s.open <= 0.0f) {
        JsonArray opens = doc["chart"]["result"][0]["indicators"]["quote"][0]["open"];
        if (!opens.isNull() && opens.size() > 0 && !opens[0].isNull())
            s.open = opens[0].as<float>();
    }

    s.prevClose = meta["chartPreviousClose"]         | 0.0f;
    s.change    = s.price - s.prevClose;
    s.changePct = (s.prevClose > 0.0f)
                    ? (s.change / s.prevClose * 100.0f) : 0.0f;

    if (s.open > 0.0f) {  // s.open already set above
        s.changeFromOpen    = s.price - s.open;
        s.changePctFromOpen = s.changeFromOpen / s.open * 100.0f;
    } else {
        s.changeFromOpen    = 0.0f;
        s.changePctFromOpen = 0.0f;
    }

    s.valid = (s.price > 0.0f);
    Serial.printf("[StockAPI] %s → $%.2f (open $%.2f, prev $%.2f)\n",
                  sym, s.price, s.open, s.prevClose);
    return s.valid;
}

bool StockAPI::fetchQuotes(const char* symbols[], int count, StockData results[]) {
    if (WiFi.status() != WL_CONNECTED || count == 0) return false;

    bool anyValid = false;
    for (int i = 0; i < count; i++) {
        results[i].valid = false;
        if (fetchOne(symbols[i], results[i]))
            anyValid = true;
    }
    return anyValid;
}
