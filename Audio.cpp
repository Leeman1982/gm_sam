// ============================================================================
//  Audio.cpp  -  I2S / PWM output pump (see Audio.h)
// ============================================================================
#include "Audio.h"
#include "Config.h"
#include "Synth.h"

#if AUDIO_BACKEND_I2S
  #include <I2S.h>
  static I2S i2s(OUTPUT);
#elif AUDIO_BACKEND_PWM
  #include <PWMAudio.h>
  static PWMAudio pwm(PIN_PWM_L, true /* stereo: uses PIN_PWM_L and +1 */);
#else
  #error "Select an audio backend in Config.h (AUDIO_BACKEND_I2S or _PWM)"
#endif

namespace Audio {

void begin(){
#if AUDIO_BACKEND_I2S
  i2s.setBCLK(PIN_I2S_BCLK);          // LRCLK is forced to BCLK+1 by the core
  i2s.setDATA(PIN_I2S_DATA);
  i2s.setBitsPerSample(16);
  i2s.setStereo(true);
  i2s.setBuffers(8, AUDIO_BLOCK);     // a few blocks of latency, glitch-free
  i2s.begin(AUDIO_RATE);
#elif AUDIO_BACKEND_PWM
  pwm.setBuffers(8, AUDIO_BLOCK);
  pwm.begin(AUDIO_RATE);
#endif
}

void service(){
  static int16_t buf[AUDIO_BLOCK * 2];     // interleaved L,R
  const int needBytes = AUDIO_BLOCK * 2 * sizeof(int16_t);

#if AUDIO_BACKEND_I2S
  if (i2s.availableForWrite() < needBytes) return;
  Synth::render(buf, AUDIO_BLOCK);
  i2s.write((const uint8_t*)buf, needBytes);
#elif AUDIO_BACKEND_PWM
  if (pwm.availableForWrite() < needBytes) return;
  Synth::render(buf, AUDIO_BLOCK);
  pwm.write((const uint8_t*)buf, needBytes);
#endif
}

} // namespace Audio
