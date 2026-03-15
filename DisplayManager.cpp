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

    if (_p.displayAnimate()) {
        if (_looping) {
            _p.displayReset();
        } else {
            _done = true;
            if (_mode == MODE_VERT_ENTER) {
                _enterDone = true;
                _mode      = MODE_NORMAL;   // text now static; no further ticking needed
            } else if (_mode == MODE_VERT_EXIT) {
                _exitDone = true;
                _mode     = MODE_NORMAL;
            }
        }
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
    // Swipe in from top; PA_PRINT out = text stays after arriving (no visual change)
    _p.displayText(_buf, PA_CENTER, VERTICAL_SPEED_MS, 0, PA_SCROLL_DOWN, PA_PRINT);
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
