#include "StockAPI.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>

// Yahoo Finance requires a session cookie + crumb since ~2024.
// The crumb is fetched once and reused; it stays valid for ~24 h.
static String s_cookie;
static String s_crumb;
static unsigned long s_crumbFetchedMs = 0;
static const unsigned long CRUMB_TTL_MS = 20UL * 3600UL * 1000UL;  // 20 h

static void addCommonHeaders(HTTPClient& h) {
    h.addHeader(F("User-Agent"),
        F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
          "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    h.addHeader(F("Accept"), F("application/json"));
}

// Obtains a session cookie from finance.yahoo.com and then exchanges it for a crumb.
// The crumb must be appended as &crumb=<value> on every chart API call.
static bool fetchCrumb() {
    Serial.printf("[StockAPI] fetchCrumb() heap:%u\n", ESP.getFreeHeap());

    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);

    HTTPClient https;
    https.setTimeout(30000);
    https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    addCommonHeaders(https);

    // Step 1 — main Finance page; sets A1/A3 session cookie
    if (!https.begin(client, F("https://finance.yahoo.com"))) {
        Serial.println(F("[StockAPI] crumb s1 begin() failed"));
        return false;
    }
    const char* wantHeaders[] = { "Set-Cookie" };
    https.collectHeaders(wantHeaders, 1);

    int code = https.GET();
    Serial.printf("[StockAPI] crumb s1 HTTP %d\n", code);

    String raw = https.header("Set-Cookie");
    https.end();

    if (raw.isEmpty()) {
        Serial.println(F("[StockAPI] No Set-Cookie from finance.yahoo.com"));
        return false;
    }
    // Keep only the name=value pair (before first ';')
    int semi = raw.indexOf(';');
    s_cookie = (semi > 0) ? raw.substring(0, semi) : raw;
    Serial.printf("[StockAPI] cookie: %s\n", s_cookie.c_str());

    delay(300);
    yield();

    // Step 2 — exchange cookie for crumb (use query1 — less rate-limited)
    if (!https.begin(client,
            F("https://query1.finance.yahoo.com/v1/test/getcrumb"))) {
        Serial.println(F("[StockAPI] crumb s2 begin() failed"));
        return false;
    }
    https.addHeader(F("Cookie"), s_cookie);

    code = https.GET();
    Serial.printf("[StockAPI] crumb s2 HTTP %d\n", code);

    if (code == HTTP_CODE_OK) {
        s_crumb = https.getString();
        s_crumb.trim();
    }
    https.end();

    if (s_crumb.isEmpty()) {
        Serial.println(F("[StockAPI] crumb empty after step2"));
        return false;
    }
    s_crumbFetchedMs = millis();
    Serial.printf("[StockAPI] crumb OK: %s\n", s_crumb.c_str());
    return true;
}

static bool ensureCrumb() {
    if (!s_crumb.isEmpty() &&
        (millis() - s_crumbFetchedMs) < CRUMB_TTL_MS)
        return true;
    return fetchCrumb();
}

// Fetches a single symbol via the Yahoo Finance v8 chart endpoint.
// Parses: price, open, prevClose from the chart meta block.
static bool fetchOne(const char* sym, StockData& s) {
    Serial.printf("\n[StockAPI] === %s  heap:%u ===\n", sym, ESP.getFreeHeap());

    String url = F("https://query2.finance.yahoo.com/v8/finance/chart/");
    for (const char* p = sym; *p; p++) {
        if (*p == '^') url += F("%5E");
        else           url += *p;
    }
    url += F("?interval=1d&range=1d&crumb=");
    url += s_crumb;
    Serial.println(url);

    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);

    HTTPClient https;
    https.setTimeout(30000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    addCommonHeaders(https);
    https.addHeader(F("Cookie"), s_cookie);

    if (!https.begin(client, url)) {
        Serial.printf("[StockAPI] begin() FAILED for %s\n", sym);
        return false;
    }

    int code = https.GET();
    Serial.printf("[StockAPI] HTTP %d\n", code);

    if (code != HTTP_CODE_OK) {
        String errSnip;
        auto& st = https.getStream();
        while (st.available() && errSnip.length() < 200)
            errSnip += (char)st.read();
        Serial.printf("[StockAPI] body: %s\n", errSnip.c_str());
        https.end();
        return false;
    }

    StaticJsonDocument<256> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["regularMarketOpen"]  = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, https.getStream(),
                                               DeserializationOption::Filter(filter));
    https.end();

    Serial.printf("[StockAPI] parse: %s\n", err ? err.c_str() : "OK");
    if (err) return false;

    Serial.print(F("[StockAPI] filtered: "));
    serializeJson(doc, Serial);
    Serial.println();

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) {
        Serial.printf("[StockAPI] %s — meta null\n", sym);
        return false;
    }

    strlcpy(s.symbol, sym, MAX_TICKER_LEN);
    s.price = meta["regularMarketPrice"] | 0.0f;
    if (s.price <= 0.0f)
        s.price = meta["chartPreviousClose"] | 0.0f;

    s.open      = meta["regularMarketOpen"]    | 0.0f;
    s.prevClose = meta["chartPreviousClose"]   | 0.0f;
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

    // Refresh crumb if absent or expired; if it fails, abort early.
    if (!ensureCrumb()) {
        Serial.println(F("[StockAPI] Could not obtain crumb — aborting fetch"));
        return false;
    }

    bool anyValid = false;
    for (int i = 0; i < count; i++) {
        results[i].valid = false;
        if (fetchOne(symbols[i], results[i])) {
            anyValid = true;
        } else {
            // On timeout / auth failure, clear crumb so next cycle re-fetches it
            if (s_crumb.length() > 0) {
                Serial.println(F("[StockAPI] Clearing crumb after fetch failure"));
                s_crumb = "";
            }
        }
        delay(300);
        yield();
    }
    return anyValid;
}
