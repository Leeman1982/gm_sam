// ============================================================================
//  GMSynth.h  -  GM voice backend (UNCHANGED API, SoundFont implementation)
//
//  Historically this drove a Dream SAM2695 over serial MIDI. It now drives the
//  INTERNAL SoundFont engine (SoundFont.cpp -> tsf -> I2S DAC). The public API
//  is identical so the sequencer engine and UI did not change: every voice
//  still flows through these calls.
//
//  The calls are non-blocking: they push MIDI messages into the SoundFont
//  engine's lock-free ring, which the audio render context consumes. The MIDI
//  real-time helpers (clockTick/start/stop/cont) are no-ops now - there is no
//  external gear to clock.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace GMSynth {

  // Retained for symmetry. The SoundFont engine + I2S audio are brought up from
  // the sketch (SoundFont::begin / AudioOut::begin); this is now a no-op.
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

} // namespace GMSynth
