#include "StockAPI.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>

// Yahoo Finance v8/chart — no crumb or cookie needed when the right
// User-Agent + Referer headers are supplied.  The chart endpoint is
// less strictly gated than the quote/getcrumb endpoints and returns
// the same meta block (regularMarketPrice, open, prevClose) for both
// market-hours and after-hours requests.

// NOTE: must be called AFTER https.begin() — HTTPClient internals are
// not initialised until begin() is called; adding headers before it
// causes a null-pointer crash on ESP8266 HTTPClient 3.x.
static void addCommonHeaders(HTTPClient& h) {
    h.addHeader(F("User-Agent"),
        F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
          "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"));
    h.addHeader(F("Accept"),          F("application/json, text/plain, */*"));
    h.addHeader(F("Accept-Language"), F("en-US,en;q=0.9"));
    h.addHeader(F("Referer"),         F("https://finance.yahoo.com/"));
    h.addHeader(F("Origin"),          F("https://finance.yahoo.com"));
}

// Fetches a single symbol via the Yahoo Finance v8 chart endpoint.
// No cookie or crumb required — uses anonymous access with browser-like headers.
static bool fetchOne(const char* sym, StockData& s) {
    Serial.printf("\n[StockAPI] === %s  heap:%u ===\n", sym, ESP.getFreeHeap());

    // Build URL — percent-encode '^' for index symbols like ^DJI
    String url = F("https://query2.finance.yahoo.com/v8/finance/chart/");
    for (const char* p = sym; *p; p++) {
        if (*p == '^') url += F("%5E");
        else           url += *p;
    }
    url += F("?interval=1d&range=1d");  // range=1d → chartPreviousClose = prior session close (1-day change)
    Serial.println(url);

    BearSSL::WiFiClientSecure client;
    client.setInsecure();   // Skip cert validation — saves ~20 KB heap vs fingerprint
    client.setTimeout(30);

    HTTPClient https;
    https.setTimeout(30000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    // begin() MUST come before addHeader() — see note on addCommonHeaders()
    if (!https.begin(client, url)) {
        Serial.printf("[StockAPI] begin() FAILED for %s\n", sym);
        return false;
    }
    addCommonHeaders(https);

    int code = https.GET();
    Serial.printf("[StockAPI] HTTP %d\n", code);

    if (code != HTTP_CODE_OK) {
        // Log up to 300 chars of the error body for diagnosis
        String errSnip;
        auto& st = https.getStream();
        while (st.available() && errSnip.length() < 300)
            errSnip += (char)st.read();
        Serial.printf("[StockAPI] error body: %s\n", errSnip.c_str());
        https.end();
        return false;
    }

    // static: lives in BSS (data segment) rather than the stack, avoiding
    // stack-overflow crashes when combined with BearSSL's large stack footprint.
    static StaticJsonDocument<512>  filter;
    static StaticJsonDocument<1024> doc;
    filter.clear();
    doc.clear();
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]         = true;
    filter["chart"]["result"][0]["meta"]["regularMarketOpen"]          = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]         = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPreviousClose"] = true;
    DeserializationError err = deserializeJson(
        doc, https.getStream(), DeserializationOption::Filter(filter));
    https.end();

    Serial.printf("[StockAPI] parse: %s\n", err ? err.c_str() : "OK");
    if (err) return false;

    Serial.print(F("[StockAPI] filtered JSON: "));
    serializeJson(doc, Serial);
    Serial.println();

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) {
        Serial.printf("[StockAPI] %s — meta null (symbol wrong or delisted?)\n", sym);
        return false;
    }

    strlcpy(s.symbol, sym, MAX_TICKER_LEN);
    s.price = meta["regularMarketPrice"] | 0.0f;

    // prevClose: prefer chartPreviousClose (adjusted), fall back to regularMarketPreviousClose
    s.prevClose = meta["chartPreviousClose"] | 0.0f;
    if (s.prevClose <= 0.0f)
        s.prevClose = meta["regularMarketPreviousClose"] | 0.0f;

    // If price still zero (weekend / extended hours gap), use prevClose as display price
    if (s.price <= 0.0f)
        s.price = s.prevClose;

    s.open      = meta["regularMarketOpen"] | 0.0f;
    s.change    = s.price - s.prevClose;
    s.changePct = (s.prevClose > 0.0f) ? (s.change / s.prevClose * 100.0f) : 0.0f;

    if (s.open > 0.0f) {
        s.changeFromOpen    = s.price - s.open;
        s.changePctFromOpen = s.changeFromOpen / s.open * 100.0f;
    } else {
        s.changeFromOpen = s.changePctFromOpen = 0.0f;
    }

    s.valid = (s.price > 0.0f);
    Serial.printf("[StockAPI] %s → price=%.2f open=%.2f prev=%.2f valid=%d\n",
                  sym, s.price, s.open, s.prevClose, (int)s.valid);
    return s.valid;
}

bool StockAPI::fetchQuotes(const char* symbols[], int count, StockData results[]) {
    if (WiFi.status() != WL_CONNECTED || count == 0) return false;

    bool anyValid = false;
    for (int i = 0; i < count; i++) {
        results[i].valid = false;
        if (fetchOne(symbols[i], results[i])) {
            anyValid = true;
        }
        delay(400);  // Small gap between requests to avoid per-IP rate limiting
        yield();
    }
    return anyValid;
}
