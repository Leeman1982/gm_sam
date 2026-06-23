// ============================================================================
//  UI.h  -  SH1106 OLED interface + input state machine (core0 only)
//
//  Renders the current page with U8g2 (full-frame buffer, ~30 fps) and turns
//  encoder / button events from Controls into edits on the global `seq`.
//
//  Control model (consistent on every page):
//    rotate          move cursor / selection
//    SHIFT + rotate  change the value at the cursor
//    click           primary action  (SEQ: toggle step | SONG: run action row)
//    SHIFT + click   secondary action (SEQ: cycle the per-step field)
//    long-press enc  audition / preview the selected note
//    PLAY            start / stop          (SHIFT = start from top)
//    PAGE            next page             (SHIFT = previous page)
//    TRACK           next track            (SHIFT = previous, long = clear track)
//    MUTE            mute track            (SHIFT = solo track)
// ============================================================================
#pragma once
#include <Arduino.h>

namespace UI {
  void begin();        // configure I2C + U8g2 and reset UI state
  void handleInput();  // consume Controls events -> edits / navigation
  void render();       // draw the current page (call at ~30 fps)
}
