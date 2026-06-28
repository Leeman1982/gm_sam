// ============================================================================
//  GMSynth.h  -  VS1053B GM-synth driver (SPI / real-time MIDI)
//
//  Thin, allocation-free wrapper that brings the VS1053B up in real-time MIDI
//  mode (software start patch) and streams 0x00-padded MIDI bytes over the SDI
//  (XDCS) data bus. The public API below is identical to the previous SAM2695
//  UART driver, so the sequencer/UI are unchanged.
//  IMPORTANT: only ONE core may call these functions. In this project that is
//  core1 (the sequencer engine). core0 never touches the SPI bus.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace GMSynth {

  // Bring up SPI, reset the VS1053B, set the clock, and load the real-time MIDI
  // start patch so the chip is ready to receive MIDI on the SDI bus.
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

  void gmReset();                          // emulated GM reset (silence + CC121)
  void masterVolume(uint8_t v);            // native VS1053 SCI_VOL (0..127)

  // Diagnostic counter: total Note-On messages sent, for an on-screen MIDI-
  // activity indicator. Written by core1, read-only from core0.
  extern volatile uint32_t notesSent;

  // Boot read-back (refreshed by begin()/serviceHealth()): VS1053 chip version
  // (4 = VS1053, 3 = VS1003, 0/15 = no SPI reply) and SCI_AUDATA (0xAC45 once
  // RT-MIDI is live). vsAlive is true once the chip is confirmed in RT-MIDI mode;
  // vsJustCameAlive pulses on the dead->alive edge so the engine can resend.
  extern volatile uint16_t vsVersion;
  extern volatile uint16_t vsAudata;
  extern volatile bool     vsAlive;
  extern volatile bool     vsJustCameAlive;

  // Call from core1's loop: while the chip isn't alive, periodically re-attempt
  // the bring-up (drives the live OLED diagnostic + auto-recovery). No-op once up.
  void serviceHealth();

} // namespace GMSynth
