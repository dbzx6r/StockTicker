#pragma once

#include <Arduino.h>
#include "config.h"

#define MAX_SSID_LEN    33
#define MAX_PASS_LEN    65

struct AppConfig {
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASS_LEN];
    char stocks[MAX_STOCKS][MAX_TICKER_LEN];
    int  stockCount;
};

class Storage {
public:
    // Mount LittleFS. Must be called before any other method.
    static bool begin();

    // Returns true if a saved config file exists.
    static bool hasConfig();

    // Load config from flash. Returns false if missing or corrupt.
    static bool loadConfig(AppConfig& cfg);

    // Persist config to flash. Returns false on write error.
    static bool saveConfig(const AppConfig& cfg);

    // Delete the saved config (triggers setup mode on next boot).
    static void clearConfig();
};
