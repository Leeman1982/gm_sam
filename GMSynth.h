// ============================================================================
//  GMSynth.h  -  GM voice API (compatibility front-end)
//
//  Historically this drove an external SAM2695 over serial MIDI.  It now
//  forwards to the ON-CHIP SoundFont synth (see GMSynth.cpp -> Synth.*), so the
//  sequencer engine and UI did not have to change.  Reverb/chorus and MIDI
//  real-time/transport calls are accepted but inert (no outboard module / FX).
//  IMPORTANT: only ONE core may call these functions -- core1 (the engine).
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace GMSynth {

  // Bring up Serial1 on the configured pins and reset the SAM2695 to GM mode.
  void begin();

  // ---- Channel voice messages (channel is 1-based, 1..16) ----
  void noteOn (uint8_t ch, uint8_t note, uint8_t vel);
  void noteOff(uint8_t ch, uint8_t note);
  void controlChange(uint8_t ch, uint8_t cc, uint8_t value);
  void programChange(uint8_t ch, uint8_t program);
  void pitchBend(uint8_t ch, int16_t bend14);     // -8192..+8191, 0 = centre

  // ---- Convenience wrappers for the SAM2695 GM feature set ----
  void bankSelect(uint8_t ch, uint8_t bankMSB);   // 0 = GM, 127 = MT-32
  void setVolume(uint8_t ch, uint8_t v);          // CC7
  void setPan(uint8_t ch, uint8_t v);             // CC10 (64 = centre)
  void setExpression(uint8_t ch, uint8_t v);      // CC11
  void setModulation(uint8_t ch, uint8_t v);      // CC1
  void setReverbSend(uint8_t ch, uint8_t v);      // CC91
  void setChorusSend(uint8_t ch, uint8_t v);      // CC93
  void setReverbType(uint8_t ch, uint8_t t);      // CC80, 0..7
  void setChorusType(uint8_t ch, uint8_t t);      // CC81, 0..7

  // ---- Real-time / system ----
  void clockTick();        // 0xF8
  void start();            // 0xFA
  void stop();             // 0xFC
  void cont();             // 0xFB (continue)

  void allNotesOff(uint8_t ch);   // CC123
  void allSoundOff(uint8_t ch);   // CC120
  void panic();                   // notes+sound off on all 16 channels

  void gmReset();                          // GM-On SysEx (F0 7E 7F 09 01 F7)
  void masterVolume(uint8_t v);            // GM master volume SysEx (0..127)

  void setFont(uint8_t index);             // switch the active on-chip SoundFont

} // namespace GMSynth
