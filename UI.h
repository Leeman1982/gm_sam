// ============================================================================
//  UI.h  -  SH1106 OLED interface + input state machine (core0 only)
//
//  Renders the current page with U8g2 (full-frame buffer, ~30 fps) and turns
//  encoder / button / keypad / pot events into edits on the global `seq`.
//
//  Control model:
//    rotate            move cursor / selection
//    click             SEQ: toggle step | menu: enter/exit value-edit mode
//                      | action rows (">"): execute
//    hold BACK         SHIFT modifier
//    SHIFT + rotate    change the value at the cursor directly
//    SHIFT + click     SEQ: cycle the per-step field | PERF: solo track
//    long-press enc    audition / preview the selected note
//    BACK (short)      exit edit mode, else next page
//    CONFIRM           play / stop   (SHIFT = start from top, long = PANIC)
//    KEYPAD key        SEQ: toggle step | PERF: mute | menu pages: pick track
//    SHIFT + key       pick track 1..16 (any page)
//    hold key + rotate SEQ: edit that step's selected field
//    long-press key    SEQ: cursor to step + audition | PERF: solo
//    POT 1 / POT 2     macro control for the selected track
//                      (melodic: cutoff / resonance - drums: rev send / volume)
// ============================================================================
#pragma once
#include <Arduino.h>

namespace UI {
  void begin();        // configure I2C + U8g2 and reset UI state
  void handleInput();  // consume input events -> edits / navigation
  void render();       // draw the current page (call at ~30 fps)
}
