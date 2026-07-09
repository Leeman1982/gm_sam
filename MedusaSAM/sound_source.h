#pragma once
// ============================================================================
//  Medusa SAM  --  sound_source.h
//  Note-event routing layer between the sequencer engine and sound hardware.
//
//  Today there is one destination: the SAM2695 over serial MIDI (DEST_SAM).
//  A second destination (DEST_LAYER) is reserved for the planned onboard
//  "baked" synth / SoundFont voice -- the Medusa GM engine rendered over I2S
//  on GP15..17, which this pinout keeps free.  To add it later:
//
//    1. Implement the three hooks below in a new layer_synth.cpp
//       (render audio on spare core1 time or a PIO/DMA callback).
//    2. Nothing else changes: the engine already routes every note event
//       through these functions by TrackCfg::dest, and the UI already lets
//       each track select its destination (INST page, "Out" field).
//
//  The hooks are called ONLY from core1, at event-fire time (post swing /
//  micro-timing / ratchet), so a future layer gets the exact same groove.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "sam2695.h"

namespace SoundSource {

  inline void noteOn(uint8_t dest, uint8_t ch, uint8_t note, uint8_t vel) {
    if (dest == DEST_SAM) SAM::noteOn(ch, note, vel);
    // else DEST_LAYER: future baked synth voice-on goes here
  }

  inline void noteOff(uint8_t dest, uint8_t ch, uint8_t note) {
    if (dest == DEST_SAM) SAM::noteOff(ch, note);
    // else DEST_LAYER: future baked synth voice-off goes here
  }

  inline void allOff() {
    SAM::panic();
    // future baked synth: silence its voice pool here
  }

} // namespace SoundSource
