#pragma once

#include <Arduino.h>
#include "Storage.h"

class WiFiSetup {
public:
    // Start the captive-portal hotspot and web server.
    // Call once — the /save handler reboots the device after writing config.
    static void begin(AppConfig& cfg);

    // Process pending DNS queries and HTTP requests. Call every loop() iteration.
    static void handle();
};
