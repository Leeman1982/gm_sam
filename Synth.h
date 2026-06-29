// ============================================================================
//  Synth.h  -  On-chip SoundFont voice engine (replaces the external GM chip)
//
//  Turns MIDI-style note/controller calls into sample-playback voices read
//  straight from the SoundFont in flash, mixes them to a stereo buffer.  All
//  calls (note on/off, controllers, render) happen on core1 only -- the same
//  core that used to own the MIDI UART -- so no locking is required.
//
//  Fixed-point throughout the hot loop (no FPU on the RP2040): a 32.32 phase
//  accumulator indexes the PCM, a Q16 linear DAHDSR drives amplitude.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace Synth {

  // Bring up the SoundFont reader; returns false if the baked font is unusable.
  bool begin();

  // ---- channel voice messages (channel 1..16, matching the old GM driver) --
  void noteOn (uint8_t ch, uint8_t note, uint8_t vel);
  void noteOff(uint8_t ch, uint8_t note);
  void programChange(uint8_t ch, uint8_t program);
  void bankSelect(uint8_t ch, uint8_t bankMSB);
  void pitchBend(uint8_t ch, int16_t bend14);          // -8192..+8191

  void setVolume(uint8_t ch, uint8_t v);               // CC7
  void setPan(uint8_t ch, uint8_t v);                  // CC10 (64 = centre)
  void setExpression(uint8_t ch, uint8_t v);           // CC11

  void allNotesOff(uint8_t ch);
  void allSoundOff(uint8_t ch);
  void panic();
  void reset();                                        // GM reset of all state
  void masterVolume(uint8_t v);                        // 0..127

  // Look up the real SoundFont patch name for the UI (or nullptr).
  const char* patchName(uint8_t ch, uint8_t program);

  // ---- runtime font selection ----------------------------------------------
  // Several fonts can be resident at once; switch the active one live.
  // setFont() must run on core1 (it reloads the shared reader + kills voices);
  // route UI requests through the engine.  The query helpers are read-only and
  // safe to call from core0.
  uint8_t     fontCount();
  uint8_t     currentFont();
  const char* fontName(uint8_t index);
  bool        setFont(uint8_t index);

  // ---- audio rendering (called from the audio pump on core1) ---------------
  // Fills `frames` interleaved L,R int16 samples at AUDIO_RATE.
  void render(int16_t* out, uint32_t frames);

  uint8_t activeVoices();   // for diagnostics / the UI status bar

} // namespace Synth
