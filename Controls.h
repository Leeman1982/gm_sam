// ============================================================================
//  Controls.h  -  Rotary encoder (interrupt quadrature) + debounced buttons
//                 Runs entirely on core0.
//
//  Physical controls (all on the OLED module unless noted):
//    encoder rotate / push (TRA/TRB/PSH)
//    CONFIRM (CON) : play/stop, SHIFT = from top, long = PANIC
//    BACK    (BAK) : short press = back / next page, HOLD = SHIFT modifier.
//                    When BACK is used as a modifier (something else happens
//                    while it is held) the short-press edge is suppressed.
//    optional aux buttons PAGE / TRACK / MUTE on GP11..GP13.
// ============================================================================
#pragma once
#include <Arduino.h>

namespace Controls {
  void begin();
  void update();            // call every core0 loop iteration

  int  encDelta();          // net detents since last call (+CW / -CCW), then clears
  bool encClick();          // short click edge (consumed)
  bool encLongPress();      // long-hold edge (consumed)

  bool shiftHeld();         // BACK held = SHIFT modifier level
  bool backPressed();       // BACK short-press edge (consumed, auto-suppressed
                            // if BACK acted as a modifier during the hold)
  void suppressBack();      // call when SHIFT was consumed while BACK held

  bool confirmPressed();    // CONFIRM short edge (consumed)
  bool confirmLong();       // CONFIRM long-hold edge (consumed)

  // optional aux buttons (never fire if unwired - inputs are pulled up)
  bool pagePressed();
  bool trackPressed();
  bool trackLongPress();
  bool mutePressed();
}
