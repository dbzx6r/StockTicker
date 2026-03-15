#pragma once

#include <Arduino.h>
#include "config.h"

struct StockData {
    char  symbol[MAX_TICKER_LEN];
    float price;              // Current / last traded price
    float open;               // Day open price
    float prevClose;          // Previous session close
    float change;             // $ change vs. prev close
    float changePct;          // % change vs. prev close
    float changeFromOpen;     // price - open  (computed)
    float changePctFromOpen;  // (price - open) / open * 100  (computed)
    bool  valid;
};

class StockAPI {
public:
    // Fetch live quotes for `count` symbols via Yahoo Finance (no API key needed).
    // `results` must be at least `count` elements.
    // Returns true if at least one quote was successfully parsed.
    static bool fetchQuotes(const char* symbols[], int count, StockData results[]);
};
