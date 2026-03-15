#include "DisplayManager.h"

// Hardware SPI: DIN=D7(GPIO13), CLK=D5(GPIO14), CS=D8(GPIO15)
MD_Parola DisplayManager::_p(MAX_HARDWARE_TYPE, MAX_CS_PIN, MAX_PANELS);
char          DisplayManager::_buf[128];
char          DisplayManager::_currentMsg[128] = "";
bool          DisplayManager::_looping     = false;
bool          DisplayManager::_done        = false;
bool          DisplayManager::_enterDone   = false;
bool          DisplayManager::_exitDone    = false;
DisplayManager::Mode DisplayManager::_mode = DisplayManager::MODE_NORMAL;
bool          DisplayManager::_flashOn     = false;
unsigned long DisplayManager::_flashToggle = 0;
unsigned long DisplayManager::_enterAnimMs = 0;

void DisplayManager::begin() {
    _p.begin();
    _p.setIntensity(DISPLAY_INTENSITY);
    _p.displayClear();
}

void DisplayManager::update() {
    if (_mode == MODE_FLASH) {
        // Manual blink: toggle every 700 ms between text and blank
        unsigned long now = millis();
        if (now - _flashToggle >= 700UL) {
            _flashToggle = now;
            _flashOn     = !_flashOn;
            if (_flashOn) {
                _p.displayText(_buf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
            } else {
                _p.displayClear();
            }
        }
        _p.displayAnimate();
        return;
    }

    // MODE_STATIC: text is frozen on the MAX7219 hardware — no ticking needed.
    // verticalExit() will override this by calling _p.displayText() directly.
    if (_mode == MODE_STATIC) return;

    if (_p.displayAnimate()) {
        if (_looping) {
            _p.displayReset();
        } else {
            _done = true;
            // MODE_VERT_ENTER: handled by the timer below; if displayAnimate()
            // somehow returns true before the timer fires (e.g. very slow scroll),
            // fall back to marking enter done here too.
            if (_mode == MODE_VERT_ENTER) {
                _enterDone = true;
                _mode      = MODE_STATIC;
            } else if (_mode == MODE_VERT_EXIT) {
                _exitDone = true;
                _mode     = MODE_NORMAL;
            }
        }
    }

    // Timer-based enter-done: fire once the scroll-in animation has had enough
    // time to complete (VERTICAL_SPEED_MS × 10 ≈ 250 ms for an 8-row display).
    // At this point the text is visible on screen; set MODE_STATIC so we stop
    // calling displayAnimate() and the MAX7219 hardware holds the image.
    if (_mode == MODE_VERT_ENTER && !_enterDone &&
            millis() - _enterAnimMs >= (unsigned long)VERTICAL_SPEED_MS * 10) {
        _enterDone = true;
        _mode      = MODE_STATIC;
    }
}

void DisplayManager::scrollText(const char* msg, bool loop) {
    _mode    = MODE_NORMAL;
    _looping = loop;
    _done    = false;
    _currentMsg[0] = '\0';  // Reset guard — next staticText() must redraw
    strlcpy(_buf, msg, sizeof(_buf));
    _p.displayClear();
    _p.displayText(_buf, PA_LEFT, SCROLL_SPEED_MS, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

void DisplayManager::staticText(const char* msg) {
    // Skip redraw if the display already shows this exact message
    if (strcmp(msg, _currentMsg) == 0) return;

    _mode    = MODE_NORMAL;
    _looping = false;
    _done    = false;
    strlcpy(_buf, msg, sizeof(_buf));
    strlcpy(_currentMsg, msg, sizeof(_currentMsg));
    _p.displayClear();
    // PA_PRINT renders text immediately; displayAnimate() finalises on first call
    _p.displayText(_buf, PA_CENTER, 0, 2000, PA_PRINT, PA_NO_EFFECT);
    _p.displayAnimate();
}

void DisplayManager::flashText(const char* msg) {
    _mode      = MODE_FLASH;
    _looping   = true;
    _done      = false;
    _enterDone = false;
    _exitDone  = false;
    _flashOn   = true;
    _flashToggle   = millis();
    _currentMsg[0] = '\0';
    strlcpy(_buf, msg, sizeof(_buf));
    _p.displayClear();
    _p.displayText(_buf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
}

void DisplayManager::verticalEnter(const char* msg) {
    _mode      = MODE_VERT_ENTER;
    _looping   = false;
    _done      = false;
    _enterDone = false;
    _exitDone  = false;
    _currentMsg[0] = '\0';
    strlcpy(_buf, msg, sizeof(_buf));
    _p.displayClear();
    // Scroll in from top.  A 60-second pause keeps the text on screen until
    // verticalExit() overrides it.  Enter-done is detected via timer (not by
    // waiting for displayAnimate() to return true) so we freeze into MODE_STATIC
    // as soon as the scroll-in completes — PA_PRINT out was avoided because it
    // clears the display on some MD_Parola builds.
    _p.displayText(_buf, PA_CENTER, VERTICAL_SPEED_MS, 60000, PA_SCROLL_DOWN, PA_SCROLL_DOWN);
    _enterAnimMs = millis();
}

void DisplayManager::verticalExit() {
    _mode     = MODE_VERT_EXIT;
    _looping  = false;
    _done     = false;
    _exitDone = false;
    // _buf still holds the current text; PA_PRINT in = re-show instantly (seamless),
    // then PA_SCROLL_DOWN sweeps it out to the bottom.
    _p.displayText(_buf, PA_CENTER, VERTICAL_SPEED_MS, 0, PA_PRINT, PA_SCROLL_DOWN);
}

bool DisplayManager::isAnimationDone() {
    return _done;
}

bool DisplayManager::isEnterDone() {
    return _enterDone;
}

bool DisplayManager::isExitDone() {
    return _exitDone;
}
