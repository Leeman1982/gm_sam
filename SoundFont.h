// ============================================================================
//  SoundFont.h  -  Internal SF2 synthesis engine (replaces the SAM2695)
//
//  Wraps TinySoundFont (https://github.com/schellingb/TinySoundFont, the
//  single-header "tsf.h"). The SF2 banks live on the external flash module;
//  this engine renders 16 MIDI-channel polyphony to a stereo PCM block that
//  AudioOut streams to the PCM5100A I2S DAC.
//
//  THREADING / TIMING MODEL (dual-core RP2350)
//    Producer : the sequencer engine (core1) calls the queue* functions.
//               They never touch tsf directly; they push compact MIDI messages
//               into a lock-free single-producer/single-consumer ring.
//    Consumer : renderBlock() runs in the I2S DMA IRQ context. It drains the
//               ring -> applies events to tsf -> renders the block.
//    => tsf is only ever touched from ONE context (audio), so no locks. The
//       ring's head/tail discipline + a memory barrier keep the cross-context
//       (and cross-core) hand-off safe.
//
//  GMSynth.cpp is the only caller of the queue* API; the rest of the project
//  is unchanged.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace SoundFont {

  // Load the active SF2 bank from flash and bring up tsf. Returns false if no
  // valid bank was found (AudioOut will then output silence).
  bool begin();

  // Switch the active bank (index into the on-flash directory). Re-inits tsf.
  bool loadBank(uint8_t index);
  const char* activeBankName();
  bool ready();

  // ---- MIDI command queue (called from the sequencer / main-loop context) ----
  // channel is 1-based (1..16) to match the GMSynth API; converted internally.
  void queueNoteOn (uint8_t ch, uint8_t note, uint8_t vel);
  void queueNoteOff(uint8_t ch, uint8_t note);
  void queueCC     (uint8_t ch, uint8_t cc, uint8_t value);
  void queueProgram(uint8_t ch, uint8_t program);   // drums auto-detected on ch10
  void queueBank   (uint8_t ch, uint8_t bankMSB);
  void queuePitch  (uint8_t ch, int16_t bend14);    // -8192..+8191
  void queueAllOff (uint8_t ch);                    // all notes/sound off, 1 channel
  void queuePanic  ();                              // silence every channel
  void queueReset  ();                              // GM reset: default all channels

  void setMasterGain(uint8_t v0_127);               // GM master volume (0..127)

  // ---- audio render (called by AudioOut from the I2S DMA callback) ----
  //  Fills `frames` interleaved stereo int16 samples. Drains the MIDI ring
  //  first so note timing lines up with the block it belongs to.
  void renderBlock(int16_t* interleavedStereo, int frames);

} // namespace SoundFont
