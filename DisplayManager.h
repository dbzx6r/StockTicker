#pragma once

#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "config.h"

class DisplayManager {
public:
    // Initialise the MAX7219 chain. Call once in setup().
    static void begin();

    // Advance the animation engine. Must be called every loop() iteration.
    static void update();

    // Scroll text left across the display.
    // loop=true → repeats indefinitely; loop=false → plays once.
    static void scrollText(const char* msg, bool loop = true);

    // Display static, centred text immediately.
    static void staticText(const char* msg);

    // Blink text on/off repeatedly (attention flash).
    static void flashText(const char* msg);

    // ── 3-phase vertical cycling API ─────────────────────────────────────────
    // Phase 1 – swipe text in from the top.  Call isEnterDone() to detect
    //           when the text has fully arrived and is static on screen.
    static void verticalEnter(const char* msg);

    // Phase 3 – swipe the current text out to the bottom.  Call isExitDone()
    //           to detect when the display is blank and ready for the next enter.
    static void verticalExit();

    // True once a verticalEnter() swipe-in has finished (text is now static).
    static bool isEnterDone();

    // True once a verticalExit() swipe-out has finished (display is blank).
    static bool isExitDone();

    // Returns true once a non-looping horizontal scroll has finished.
    static bool isAnimationDone();

private:
    enum Mode { MODE_NORMAL, MODE_FLASH, MODE_VERT_ENTER, MODE_VERT_EXIT };

    static MD_Parola     _p;
    static char          _buf[128];
    static char          _currentMsg[128]; // Guards against redundant redraws
    static bool          _looping;
    static bool          _done;
    static bool          _enterDone;
    static bool          _exitDone;
    static Mode          _mode;
    static bool          _flashOn;
    static unsigned long _flashToggle;
};
