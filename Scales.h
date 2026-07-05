// ============================================================================
//  Scales.h  -  Scale definitions + note quantization for the scale lock
//
//  A scale is a 12-bit mask, bit n = semitone n above the root is in-scale.
//  All helpers are header-only, allocation free and cheap enough to run in
//  the engine's trigger path (worst case scans +-6 semitones).
// ============================================================================
#pragma once
#include <Arduino.h>

#define SCB(n) (1u << (n))

struct ScaleDef {
  const char* name;      // short display name (<= 8 chars for the OLED rows)
  uint16_t    mask;      // 12-bit interval mask, bit 0 = root
};

static const ScaleDef kScales[] = {
  { "Chromat",  0x0FFF },
  { "Major",    SCB(0)|SCB(2)|SCB(4)|SCB(5)|SCB(7)|SCB(9)|SCB(11) },
  { "Minor",    SCB(0)|SCB(2)|SCB(3)|SCB(5)|SCB(7)|SCB(8)|SCB(10) },
  { "HarmMin",  SCB(0)|SCB(2)|SCB(3)|SCB(5)|SCB(7)|SCB(8)|SCB(11) },
  { "MeloMin",  SCB(0)|SCB(2)|SCB(3)|SCB(5)|SCB(7)|SCB(9)|SCB(11) },
  { "Dorian",   SCB(0)|SCB(2)|SCB(3)|SCB(5)|SCB(7)|SCB(9)|SCB(10) },
  { "Phrygin",  SCB(0)|SCB(1)|SCB(3)|SCB(5)|SCB(7)|SCB(8)|SCB(10) },
  { "Lydian",   SCB(0)|SCB(2)|SCB(4)|SCB(6)|SCB(7)|SCB(9)|SCB(11) },
  { "Mixolyd",  SCB(0)|SCB(2)|SCB(4)|SCB(5)|SCB(7)|SCB(9)|SCB(10) },
  { "Locrian",  SCB(0)|SCB(1)|SCB(3)|SCB(5)|SCB(6)|SCB(8)|SCB(10) },
  { "MajPent",  SCB(0)|SCB(2)|SCB(4)|SCB(7)|SCB(9) },
  { "MinPent",  SCB(0)|SCB(3)|SCB(5)|SCB(7)|SCB(10) },
  { "Blues",    SCB(0)|SCB(3)|SCB(5)|SCB(6)|SCB(7)|SCB(10) },
  { "WholeTn",  SCB(0)|SCB(2)|SCB(4)|SCB(6)|SCB(8)|SCB(10) },
  { "Dim8",     SCB(0)|SCB(2)|SCB(3)|SCB(5)|SCB(6)|SCB(8)|SCB(9)|SCB(11) },
  { "DblHarm",  SCB(0)|SCB(1)|SCB(4)|SCB(5)|SCB(7)|SCB(8)|SCB(11) },
};
#define NUM_SCALES ((uint8_t)(sizeof(kScales) / sizeof(kScales[0])))

static const char* const kRootNames[12] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static inline bool scaleContains(uint8_t note, uint8_t root, uint16_t mask) {
  return (mask >> ((note + 12 - (root % 12)) % 12)) & 1u;
}

// Snap a note to the nearest in-scale pitch (ties resolve downward).
static inline uint8_t scaleQuantize(uint8_t note, uint8_t root, uint16_t mask) {
  if (!mask) return note;
  if (scaleContains(note, root, mask)) return note;
  for (int d = 1; d <= 6; d++) {
    if (note >= d   && scaleContains((uint8_t)(note - d), root, mask)) return (uint8_t)(note - d);
    if (note + d <= 127 && scaleContains((uint8_t)(note + d), root, mask)) return (uint8_t)(note + d);
  }
  return note;
}

// Move from `note` to the next in-scale note in direction dir (+1/-1).
// Used so the encoder walks scale degrees instead of semitones when locked.
static inline uint8_t scaleStep(uint8_t note, int dir, uint8_t root, uint16_t mask) {
  if (!mask) return note;
  int n = note;
  for (int i = 0; i < 12; i++) {          // a scale always repeats within 12
    n += (dir >= 0) ? 1 : -1;
    if (n < 0 || n > 127) return note;
    if (scaleContains((uint8_t)n, root, mask)) return (uint8_t)n;
  }
  return note;
}
