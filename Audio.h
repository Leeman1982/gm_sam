// ============================================================================
//  Audio.h  -  Audio output pump for the on-chip synth
//
//  Bridges Synth::render() to the speaker.  Two interchangeable backends,
//  selected in Config.h:
//     AUDIO_BACKEND_I2S : 16-bit stereo to a PCM5102/UDA1334 DAC (recommended)
//     AUDIO_BACKEND_PWM : PWM + RC filter, no extra hardware
//
//  Runs on core1: call begin() once, then service() as often as possible from
//  the engine loop.  service() tops up the output buffer only when there is
//  room, so it never blocks the real-time sequencer for long.
// ============================================================================
#pragma once
#include <Arduino.h>

namespace Audio {
  void begin();
  void service();
}
