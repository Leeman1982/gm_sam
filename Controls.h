// ============================================================================
//  Controls.h  -  Rotary encoder (interrupt quadrature) + debounced buttons
//                 Runs entirely on core0.
// ============================================================================
#pragma once
#include <Arduino.h>

namespace Controls {
  void begin();
  void update();            // call every core0 loop iteration

  int  encDelta();          // net detents since last call (+CW / -CCW), then clears
  bool encClick();          // short click edge (consumed)
  bool encLongPress();      // long-hold edge (consumed)

  bool shiftHeld();         // SHIFT button level

  bool playPressed();       // edges (consumed on read)
  bool pagePressed();
  bool trackPressed();
  bool mutePressed();
  bool trackLongPress();    // long-hold on TRACK (consumed)
}
