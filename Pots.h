// ============================================================================
//  Pots.h  -  2 analog macro pots (core0 only)
//
//  The pots always control the two most effective parameters for the track
//  that is currently selected in the UI:
//      melodic track :  POT1 = filter cutoff      POT2 = filter resonance
//      drum track    :  POT1 = reverb send        POT2 = track volume
//
//  Soft pickup: after a track change a pot is unlatched so the sound doesn't
//  jump; it re-latches (and starts writing the parameter) once the knob
//  crosses the parameter's current value, or reaches either end of its travel.
// ============================================================================
#pragma once
#include <Arduino.h>

namespace Pots {
  void begin();

  // Service both pots against the currently selected track. Returns a bitmask:
  // bit0 = pot1 wrote its parameter this call, bit1 = pot2 did. The UI uses
  // this to flash a "CUTOFF 98"-style toast.
  uint8_t update(uint8_t currentTrack);

  // Call when the selected track changes: unlatch both pots.
  void unlatch();

  // Introspection for the UI overlay.
  const char* paramName(uint8_t pot, uint8_t currentTrack);  // pot 0/1
  uint8_t     lastValue(uint8_t pot);
  bool        latched(uint8_t pot);
}
