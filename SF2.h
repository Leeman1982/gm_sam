// ============================================================================
//  SF2.h  -  Minimal, allocation-free SoundFont 2 reader for the RP2040
//
//  The SoundFont lives in flash (see Soundfont_*.cpp).  This reader keeps only
//  pointers into that flash image, so parsing costs almost no RAM and the
//  16-bit PCM is streamed straight from XIP by the synth.
//
//  It resolves a MIDI (bank, program, key, velocity) into a small set of
//  "regions" -- each one sample with the generator parameters needed to play
//  it (root key, tuning, loop points, volume envelope, pan, attenuation).
//  Preset/instrument/global-zone layering follows the SF2 2.04 spec: instrument
//  generators are absolute, preset generators are added on top.
// ============================================================================
#pragma once
#include <Arduino.h>

namespace SF2 {

// A fully resolved playable zone, ready for the synth to turn into a voice.
struct Region {
  uint32_t start, end;          // absolute int16 frame indices into the PCM pool
  uint32_t startloop, endloop;  // absolute loop points
  uint32_t sampleRate;          // sample's native rate (Hz)
  uint8_t  loopMode;            // 0 = none, 1 = continuous, 3 = loop until release
  uint8_t  rootKey;             // effective unity-pitch key
  int16_t  tuneCents;           // coarseTune*100 + fineTune + sample pitchCorrection
  int16_t  scaleTuning;         // cents of pitch change per key (default 100)
  int16_t  attenuation_cb;      // initial attenuation, centibels (>=0)
  int16_t  sustain_cb;          // volume-envelope sustain attenuation, centibels
  int16_t  pan;                 // -500 (L) .. +500 (R)
  float    delay, attack, hold, decay, release;  // volume envelope, seconds
  uint8_t  exclusiveClass;      // non-zero: cuts other voices of same class
  uint8_t  overrideKey;         // 0..127 if keynum gen forces a key, else 255
  uint8_t  overrideVel;         // 0..127 if velocity gen forces a vel, else 255
};

// Parse the flash image.  Returns false if it is not a usable SoundFont.
bool begin(const uint8_t* sf2, uint32_t len);

bool ready();

// Pointer to the start of the int16 PCM pool (in flash).  Region indices are
// frame offsets into this array.
const int16_t* pcm();
uint32_t       pcmFrames();

// Resolve a note.  Fills up to maxRegions and returns how many were produced.
// For drums pass bank = 128.  Falls back gracefully if the exact preset is
// missing (bank 0 program, then program 0).
int noteRegions(uint8_t bank, uint8_t program, uint8_t key, uint8_t vel,
                Region* out, int maxRegions);

// Human-readable preset name for a (bank, program), or nullptr.  Used by the UI
// so the INST page can show the real SoundFont patch names.
const char* presetName(uint8_t bank, uint8_t program);

} // namespace SF2
