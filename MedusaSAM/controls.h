#pragma once
// ============================================================================
//  Medusa SAM  --  controls.h
//  EC11 rotary encoder (interrupt quadrature decode + debounced push) and the
//  five Medusa buttons with short / long-press detection.  core0 only.
// ============================================================================
#include <Arduino.h>
#include "config.h"

class RotaryEncoder {
public:
    RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t pinSW);
    void begin();
    int  getDelta();            // whole detents since last call (signed)
    void update();              // poll the push switch (call from loop)
    bool wasPressed();          // short press (consumed on read)
    bool wasLongPress();        // long press (consumed on read)
    bool isHeld();
    int  rawCount() const { return (int)_count; }   // debug: raw quadrature count

private:
    static void isrTrampoline();
    static RotaryEncoder* s_self;
    void isr();

    uint8_t _pinA, _pinB, _pinSW;
    volatile int32_t _count = 0;
    volatile uint8_t _state = 0;
    // push switch
    bool _swRaw = false, _swState = false, _longFired = false;
    bool _pressedFlag = false, _longFlag = false;
    unsigned long _swDebounceT = 0, _swPressT = 0;
};

class Button {
public:
    explicit Button(uint8_t pin);
    void begin();
    void update();
    bool wasPressed();          // short press (consumed)
    bool wasLongPress();        // long press, fires once while held (consumed)
    bool isHeld();

private:
    uint8_t _pin;
    bool _lastRaw = false, _state = false;
    bool _pressedFlag = false, _longFired = false, _lpReported = false;
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
