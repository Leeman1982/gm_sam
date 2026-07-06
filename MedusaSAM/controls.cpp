#include "controls.h"

// ─── RotaryEncoder ────────────────────────────────────────────────────────────
RotaryEncoder* RotaryEncoder::s_self = nullptr;

RotaryEncoder::RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t pinSW)
    : _pinA(pinA), _pinB(pinB), _pinSW(pinSW) {}

void RotaryEncoder::begin() {
    pinMode(_pinA,  INPUT_PULLUP);
    pinMode(_pinB,  INPUT_PULLUP);
    pinMode(_pinSW, INPUT_PULLUP);
    s_self = this;
    _state = (uint8_t)((digitalRead(_pinA) << 1) | digitalRead(_pinB));
    attachInterrupt(digitalPinToInterrupt(_pinA), isrTrampoline, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinB), isrTrampoline, CHANGE);
}

void RotaryEncoder::isrTrampoline() { if (s_self) s_self->isr(); }

void RotaryEncoder::isr() {
    // Full quadrature transition table: index = (prev<<2)|curr, +1/-1/0.
    static const int8_t kTable[16] =
        { 0,-1, 1, 0,  1, 0, 0,-1,  -1, 0, 0, 1,  0, 1,-1, 0 };
    uint8_t s = (uint8_t)((digitalRead(_pinA) << 1) | digitalRead(_pinB));
    uint8_t idx = (uint8_t)(((_state << 2) | s) & 0x0F);
    _state = s;
    _count += kTable[idx];
}

int RotaryEncoder::getDelta() {
    // Emit whole detents, keep the remainder so no motion is lost.
    noInterrupts();
    int d = _count / ENC_TICKS_PER_DETENT;
    _count -= d * ENC_TICKS_PER_DETENT;
    interrupts();
    return d;
}

void RotaryEncoder::update() {
    bool raw = (digitalRead(_pinSW) == LOW);
    unsigned long now = millis();

    if (raw != _swRaw) { _swDebounceT = now; _swRaw = raw; }

    if ((now - _swDebounceT) > DEBOUNCE_MS) {
        if (raw && !_swState) {
            _swState = true; _swPressT = now; _longFired = false;
        } else if (!raw && _swState) {
            _swState = false;
            if (!_longFired) _pressedFlag = true;   // short press on release
        }
    }
    if (_swState && !_longFired && (now - _swPressT) >= LONG_PRESS_MS) {
        _longFired = true;
        _longFlag  = true;
    }
}

bool RotaryEncoder::wasPressed()   { if (_pressedFlag) { _pressedFlag = false; return true; } return false; }
bool RotaryEncoder::wasLongPress() { if (_longFlag)    { _longFlag = false;    return true; } return false; }
bool RotaryEncoder::isHeld()       { return _swState; }

// ─── Button ───────────────────────────────────────────────────────────────────
Button::Button(uint8_t pin) : _pin(pin) {}

void Button::begin() { pinMode(_pin, INPUT_PULLUP); }

void Button::update() {
    bool active = (digitalRead(_pin) == LOW);
    unsigned long now = millis();

    if (active != _lastRaw) { _lastDebounce = now; _lastRaw = active; }

    if ((now - _lastDebounce) > DEBOUNCE_MS) {
        if (active && !_state) {
            _state = true; _pressTime = now;
            _longFired = false; _lpReported = false;
        } else if (!active && _state) {
            _state = false;
            if (!_longFired) _pressedFlag = true;   // short press on release
        }
    }
    if (_state && !_longFired && (now - _pressTime) >= LONG_PRESS_MS)
        _longFired = true;
}

bool Button::wasPressed() { if (_pressedFlag) { _pressedFlag = false; return true; } return false; }
bool Button::isHeld()     { return _state; }
bool Button::wasLongPress() {
    if (_longFired && !_lpReported) { _lpReported = true; return true; }
    if (!_state) _lpReported = false;
    return false;
}

// ─── Controls ─────────────────────────────────────────────────────────────────
Controls::Controls()
    : encoder(PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW),
      play(PIN_BTN_PLAY), rec(PIN_BTN_REC), track(PIN_BTN_TRACK),
      page(PIN_BTN_PAGE), shift(PIN_BTN_SHIFT) {}

void Controls::begin() {
    encoder.begin();
    play.begin(); rec.begin(); track.begin(); page.begin(); shift.begin();
}

void Controls::update() {
    encoder.update();     // rotation is ISR-driven; this polls the push switch
    play.update(); rec.update(); track.update(); page.update(); shift.update();
}
