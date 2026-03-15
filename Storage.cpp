#include "Storage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool Storage::begin() {
    return LittleFS.begin();
}

bool Storage::hasConfig() {
    return LittleFS.exists(CONFIG_FILE);
}

bool Storage::loadConfig(AppConfig& cfg) {
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return false;

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) return false;

    strlcpy(cfg.ssid,     doc["ssid"]     | "", sizeof(cfg.ssid));
    strlcpy(cfg.password, doc["password"] | "", sizeof(cfg.password));

    cfg.stockCount = 0;
    JsonArrayConst arr = doc["stocks"].as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
        if (cfg.stockCount >= MAX_STOCKS) break;
        strlcpy(cfg.stocks[cfg.stockCount++],
                v.as<const char*>(), MAX_TICKER_LEN);
    }

    return strlen(cfg.ssid) > 0 && cfg.stockCount > 0;
}

bool Storage::saveConfig(const AppConfig& cfg) {
    StaticJsonDocument<512> doc;
    doc["ssid"]     = cfg.ssid;
    doc["password"] = cfg.password;

    JsonArray arr = doc.createNestedArray("stocks");
    for (int i = 0; i < cfg.stockCount; i++) {
        arr.add(cfg.stocks[i]);
    }

    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

void Storage::clearConfig() {
    LittleFS.remove(CONFIG_FILE);
}
