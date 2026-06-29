// ============================================================================
//  AudioOut.h  -  I2S stereo output to the PCM5100A DAC (RP2350 PIO + DMA)
//
//  Streams a continuous stereo int16 signal from the SoundFont engine to the
//  PCM5100A using the arduino-pico I2S library (PIO state machine + DMA). The
//  library's transmit callback fires when a DMA buffer drains; we refill it by
//  calling SoundFont::renderBlock(), so audio runs independently of the
//  sequencer-engine timing and the UI.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace AudioOut {
  bool begin();          // configure I2S + DMA callback, start streaming
  void stop();
  uint32_t underruns();  // diagnostic: callbacks where the DMA had no room
}
