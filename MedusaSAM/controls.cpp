#include "controls.h"

// ─── RotaryEncoder ──────────────────────────────────────────────────────────
// POLLED quadrature decode -- see controls.h for why this replaced the
// original attachInterrupt/CHANGE version (it froze the chip on rotation).
RotaryEncoder::RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t pinSW)
    : _pinA(pinA), _pinB(pinB), _pinSW(pinSW) {}

void RotaryEncoder::begin() {
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    pinMode(_pinSW, INPUT_PULLUP);
    _last = (uint8_t)((digitalRead(_pinA) << 1) | digitalRead(_pinB));
}

int RotaryEncoder::getDelta() {
    int d = _count / ENC_TICKS_PER_DETENT;
    _count -= d * ENC_TICKS_PER_DETENT;
    return d;
}

void RotaryEncoder::update() {
    // Full quadrature transition table: index = (prev<<2)|curr, +1/-1/0.
    // Same table the old ISR used; "invalid" (skipped-a-step) transitions
    // decode to 0 rather than guessing a direction, so a missed poll (e.g.
    // during a display I2C write) costs at most a lost click, never a wrong
    // direction or a runaway count.
    static const int8_t kTable[16] =
        { 0,-1, 1, 0,  1, 0, 0,-1,  -1, 0, 0, 1,  0, 1,-1, 0 };
    uint8_t s = (uint8_t)((digitalRead(_pinA) << 1) | digitalRead(_pinB));
    if (s != _last) {
        uint8_t idx = (uint8_t)(((_last << 2) | s) & 0x0F);
        _count += kTable[idx];
        _last = s;
    }

    // push switch (unchanged: plain debounce, polled)
    bool raw = (digitalRead(_pinSW) == LOW);
    unsigned long now = millis();
    if (raw != _swRaw) { _swDebounceT = now; _swRaw = raw; }
    if ((now - _swDebounceT) > DEBOUNCE_MS) {
        if (raw && !_swState) { _swState = true; _swPressT = now; _longFired = false; }
        else if (!raw && _swState) {
            _swState = false;
            if (!_longFired) _pressedFlag = true;
        }
    }
    if (_swState && !_longFired && (now - _swPressT) >= LONG_PRESS_MS) {
        _longFired = true; _longFlag = true;
    }
}

bool RotaryEncoder::wasPressed()   { if (_pressedFlag) { _pressedFlag = false; return true; } return false; }
bool RotaryEncoder::wasLongPress() { if (_longFlag)    { _longFlag = false;    return true; } return false; }
bool RotaryEncoder::isHeld()       { return _swState; }

// ─── Button ─────────────────────────────────────────────────────────────────
Button::Button(uint8_t pin, bool activeLow) : _pin(pin), _activeLow(activeLow) {}

void Button::begin() { pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN); }

void Button::update() {
    uint8_t raw = digitalRead(_pin);
    bool active = _activeLow ? (raw == LOW) : (raw == HIGH);
    unsigned long now = millis();
    if (active != _lastState) _lastDebounce = now;
    _lastState = active;
    if ((now - _lastDebounce) > DEBOUNCE_MS) {
        if (active && !_state) { _state = true; _pressTime = now; _longFired = false; _lpReported = false; }
        else if (!active && _state) {
            _state = false; _releasedFlag = true;
            if (!_longFired) _pressedFlag = true;
        }
    }
    if (_state && !_longFired && (now - _pressTime) >= LONG_PRESS_MS) _longFired = true;
}

bool Button::wasPressed()  { if (_pressedFlag)  { _pressedFlag = false;  return true; } return false; }
bool Button::wasReleased() { if (_releasedFlag) { _releasedFlag = false; return true; } return false; }
bool Button::isHeld()      { return _state; }
bool Button::wasLongPress() {
    if (_longFired && !_lpReported) { _lpReported = true; return true; }
    if (!_state) _lpReported = false;
    return false;
}

// ─── Controls ───────────────────────────────────────────────────────────────
Controls::Controls()
    : encoder(PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW),
      play(PIN_BTN_PLAY), rec(PIN_BTN_REC), track(PIN_BTN_TRACK),
      page(PIN_BTN_PAGE), shift(PIN_BTN_SHIFT) {}

void Controls::begin() {
    encoder.begin();
    play.begin(); rec.begin(); track.begin(); page.begin(); shift.begin();
}

void Controls::update() {
    encoder.update();
    play.update(); rec.update(); track.update(); page.update(); shift.update();
}
