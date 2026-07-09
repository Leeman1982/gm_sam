#pragma once
// ============================================================================
//  Medusa SAM  --  controls.h
//  Rotary encoder (POLLED quadrature decode) and debounced momentary buttons
//  with short / long-press detection.  core0 only.
//
//  The encoder was originally interrupt-driven (attachInterrupt/CHANGE on
//  A+B), matching the sibling Medusa GM build.  On this board that froze the
//  whole chip after ~1s of active rotation -- almost certainly arduino-pico's
//  shared GPIO-IRQ dispatch choking on the edge burst a mechanical encoder
//  throws off under contact bounce (a documented soft spot in that core).
//  Polling sidesteps that subsystem entirely: update() is called every
//  loop() iteration (far faster than a human can turn the knob) and reads
//  A/B directly, exactly like the on-screen diagnostic that already proved
//  this is rock solid on this hardware.
// ============================================================================
#include <Arduino.h>
#include "config.h"

class RotaryEncoder {
public:
    RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t pinSW);
    void begin();
    int  getDelta();            // detents since last call (signed)
    void update();              // poll A/B quadrature + push switch (call from loop)
    bool wasPressed();          // short press (consumed)
    bool wasLongPress();        // long press (consumed)
    bool isHeld();
    int  rawCount() const { return _count; }   // debug: raw quadrature count

private:
    uint8_t _pinA, _pinB, _pinSW;
    // No longer touched from interrupt context, so no volatile/critical
    // section is needed -- update()/getDelta() are both core0, both loop().
    int     _count = 0;
    uint8_t _last  = 0;
    // push switch state
    bool _swRaw = false, _swState = false, _longFired = false;
    bool _pressedFlag = false, _longFlag = false;
    unsigned long _swDebounceT = 0, _swPressT = 0;
};

class Button {
public:
    explicit Button(uint8_t pin, bool activeLow = true);
    void begin();
    void update();
    bool wasPressed();
    bool wasReleased();
    bool wasLongPress();
    bool isHeld();

private:
    uint8_t _pin; bool _activeLow;
    bool _lastState = false, _state = false;
    bool _pressedFlag = false, _releasedFlag = false;
    bool _longFired = false, _lpReported = false;
    unsigned long _lastDebounce = 0, _pressTime = 0;
};

class Controls {
public:
    Controls();
    void begin();
    void update();

    RotaryEncoder encoder;
    Button play, rec, track, page, shift;
};
