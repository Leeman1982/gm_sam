// ============================================================================
//  AudioOut.cpp  -  PCM5100A I2S output via the arduino-pico I2S library
//
//  Uses PIO + DMA under the hood. We register a transmit callback that the
//  library invokes from the DMA IRQ whenever an output buffer empties; in it we
//  render one block from the SoundFont engine and push it. Because the engine's
//  note events reach the renderer through the lock-free MIDI ring (see
//  SoundFont.cpp), audio output is fully decoupled from the core1 sequencer
//  loop and the core0 UI.
//
//  PCM5100A wiring (module label -> function):
//      VCC/GND power; SD = DIN (data);  BCK = bit clock;  WS/LRCK = word select;
//      MC/SCK = master clock -> leave unconnected (the PCM5100A self-clocks).
//      A library constraint: LRCLK must be BCLK+1 (see Config.h pin choices).
// ============================================================================
#include "AudioOut.h"
#include "SoundFont.h"
#include <I2S.h>

namespace {
  I2S            g_i2s(OUTPUT);
  constexpr int  FRAMES = AUDIO_BLOCK_FRAMES;
  constexpr int  WORDS  = FRAMES * AUDIO_CHANNELS;     // int16s per block (L,R...)
  int16_t        g_block[WORDS];
  volatile uint32_t g_underruns = 0;
  volatile bool  g_running = false;

  // DMA buffer empties -> render + push one block. Runs in IRQ context.
  constexpr size_t BLOCK_BYTES = (size_t)WORDS * sizeof(int16_t);

  void onTransmit() {
    if (!g_running) return;
    // Only refill when a whole block fits, so write() never blocks in the IRQ.
    // availableForWrite() is compared against the block size in bytes, which is
    // the conservative choice regardless of the library's internal unit.
    if ((size_t)g_i2s.availableForWrite() < BLOCK_BYTES) { g_underruns++; return; }
    SoundFont::renderBlock(g_block, FRAMES);
    g_i2s.write((const uint8_t*)g_block, BLOCK_BYTES);
  }
}

namespace AudioOut {

bool begin() {
  g_i2s.setBCLK(PIN_I2S_BCLK);            // LRCLK is implicitly BCLK + 1
  g_i2s.setDATA(PIN_I2S_DOUT);
  g_i2s.setBitsPerSample(16);
  g_i2s.setFrequency(AUDIO_SAMPLE_RATE);
  g_i2s.setBuffers(6, FRAMES);            // 6 DMA buffers of one block each
  g_i2s.onTransmit(onTransmit);
  if (!g_i2s.begin()) return false;

  g_running = true;
  // Prime the DMA pipeline so the first callback isn't starved.
  for (int i = 0; i < 4; ++i) {
    SoundFont::renderBlock(g_block, FRAMES);
    g_i2s.write((const uint8_t*)g_block, (size_t)WORDS * sizeof(int16_t));
  }
  return true;
}

void stop() {
  g_running = false;
  g_i2s.end();
}

uint32_t underruns() { return g_underruns; }

} // namespace AudioOut
