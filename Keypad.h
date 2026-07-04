// ============================================================================
//  Keypad.h  -  16 performance keys, core0 only.
//
//  Two supported wirings (Config.h KEYPAD_MODE):
//    1 : 4x4 matrix keypad, rows driven low one at a time, columns read.
//    2 : 16 discrete buttons through a CD74HC4067 analog mux -
//        each channel -> button -> GND, SIG pin uses the internal pull-up.
//
//  Key numbering is physical: left-to-right, top-to-bottom = key 0..15,
//  i.e. the printed [1 2 3 A / 4 5 6 B / 7 8 9 C / * 0 # D] caps map to
//  steps/tracks 1..16 regardless of the legend.
//
//  Gesture model (mirrors how the UI consumes keys):
//    tap(k)       : press + release < 450 ms with no modifier use  -> edge
//    longPress(k) : held >= 600 ms                                 -> edge
//    heldKey()    : the most recently pressed key still down, -1 if none.
//    markActive() : call when a held key was used as a modifier (e.g. hold
//                   step + turn encoder); cancels its tap AND long edges.
// ============================================================================
#pragma once
#include <Arduino.h>

namespace Keypad {
  void begin();
  void update();                 // call every core0 loop iteration

  bool tap(uint8_t k);           // consume tap edge for key 0..15
  bool longPress(uint8_t k);     // consume long-press edge
  bool isDown(uint8_t k);        // raw level
  int  heldKey();                // most recent still-held key, -1 = none
  void markActive();             // flag all held keys as "used as modifier"
}
