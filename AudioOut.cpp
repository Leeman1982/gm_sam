// ============================================================================
//  AudioOut.cpp  -  PCM5100A I2S output via the arduino-pico I2S library
//
//  Uses PIO + DMA under the hood. We register a transmit callback that the
//  library invokes from the DMA IRQ whenever output buffer space frees; in it
//  we render blocks from the SoundFont engine and push them with write16(),
//  which writes exactly one 16-bit stereo frame (left, right) - so there is no
//  sample-format or word/byte ambiguity. The availableForWrite() > 0 guard is
//  the library's documented "is there room" test and keeps write16() from
//  blocking inside the IRQ.
//
//  Because note events reach the renderer through the lock-free MIDI ring (see
//  SoundFont.cpp), audio output is fully decoupled from the core1 sequencer
//  loop and the core0 UI.
//
//  PCM5100A wiring (module label -> function):
//      VCC/GND power; SD = DIN (data); BCK = bit clock; WS/LRCK = word select;
//      MC/SCK = master clock -> leave unconnected (the PCM5100A self-clocks).
//      The library uses consecutive GPIOs for BCLK and LRCLK (LRCLK = BCLK+1).
// ============================================================================
#include "AudioOut.h"
#include "SoundFont.h"
#include <I2S.h>

namespace {
  I2S            g_i2s(OUTPUT);
  constexpr int  FRAMES = AUDIO_BLOCK_FRAMES;
  int16_t        g_block[FRAMES * AUDIO_CHANNELS];   // interleaved L,R
  int            g_pos = FRAMES;                      // force a render on first call
  volatile bool  g_running = false;

  // DMA buffer space freed -> fill it. Runs in IRQ context.
  void onTransmit() {
    if (!g_running) return;
    while (g_i2s.availableForWrite() > 0) {
      if (g_pos >= FRAMES) {                          // current block exhausted
        SoundFont::renderBlock(g_block, FRAMES);
        g_pos = 0;
      }
      g_i2s.write16(g_block[2 * g_pos], g_block[2 * g_pos + 1]);
      ++g_pos;
    }
  }
}

namespace AudioOut {

bool begin() {
  g_i2s.setBCLK(PIN_I2S_BCLK);            // LRCLK is implicitly BCLK + 1
  g_i2s.setDATA(PIN_I2S_DOUT);
  g_i2s.setBitsPerSample(16);
  g_i2s.setFrequency(AUDIO_SAMPLE_RATE);
  g_i2s.setBuffers(4, FRAMES);            // 4 DMA buffers of one block each
  g_i2s.onTransmit(onTransmit);
  if (!g_i2s.begin()) return false;
  g_running = true;                       // enable the IRQ fill path
  return true;
}

void stop() {
  g_running = false;
  g_i2s.end();
}

uint32_t underruns() { return 0; }        // reserved (see README bring-up notes)

} // namespace AudioOut
