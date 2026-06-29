// ============================================================================
//  Controls.h  -  Rotary encoder + 4x4 keypad + 3 page LEDs (core0 / main loop)
//
//  INPUT MODEL (new)
//    Rotary encoder : rotate = move/edit; SHORT TAP of its push switch = click;
//                     HOLD the push switch = SHIFT modifier (so you can hold it
//                     and turn to edit values, or hold it and press a keypad key
//                     for a shifted function). You cannot "shift+click" the same
//                     switch, so the SEQ per-step field cycle moved to a keypad
//                     key (fieldCyclePressed).
//    4x4 keypad     : three pages, shown one-hot on the 3 LEDs -
//                       KP_STEP : keys 0..15 toggle steps 0..15 of the track
//                       KP_NOTE : keys 0..15 audition notes (play the synth)
//                       KP_CTRL : keys = transport / page / track / mute, i.e.
//                                 the old hardware buttons, plus field-cycle.
//                     Select a page with SHIFT + the right-hand column keys.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace Controls {
  void begin();
  void update();            // call every main-loop iteration

  int  encDelta();          // net detents since last call (+CW / -CCW), cleared
  bool encClick();          // short-tap edge of the encoder switch (consumed)
  bool encLongPress();      // retained for API compat; always false now
  bool shiftHeld();         // encoder switch held past the SHIFT threshold

  // Transport / navigation edges (sourced from the KP_CTRL keypad page).
  bool playPressed();
  bool pagePressed();
  bool trackPressed();
  bool mutePressed();
  bool trackLongPress();    // retained; always false now

  // Keypad surface (page-aware).
  int  keypadStep();        // KP_STEP: step index 0..15 just pressed, else -1
  int  keypadNote();        // KP_NOTE: key index 0..15 just pressed, else -1
  bool fieldCyclePressed(); // KP_CTRL: cycle the SEQ per-step field
  uint8_t keypadPage();     // current KeypadPage (drives the LEDs)
}
