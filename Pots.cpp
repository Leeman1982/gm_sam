// ============================================================================
//  Pots.cpp  -  analog macro pot scanning with smoothing + soft pickup
// ============================================================================
#include "Pots.h"
#include "Config.h"
#include "Sequencer.h"

#if ENABLE_POTS

namespace {
  const uint8_t kPins[2] = { PIN_POT1, PIN_POT2 };

  struct Pot {
    uint16_t ema;        // smoothed raw reading (0..4095, EMA/4)
    uint8_t  value;      // current 0..127 position
    uint8_t  lastSent;   // last value written to the parameter
    bool     latched;
    bool     primed;     // ema seeded
  };
  Pot pots[2];

  uint8_t readPot(uint8_t i) {
    uint16_t raw = analogRead(kPins[i]);          // 12-bit on RP2040
    Pot& p = pots[i];
    if (!p.primed) { p.ema = raw; p.primed = true; }
    p.ema = (uint16_t)((p.ema * 3 + raw) / 4);    // light smoothing
    uint8_t v = (uint8_t)(p.ema >> 5);            // 4096 -> 128
    if (v > 127) v = 127;
    // +-1 hysteresis so ADC noise doesn't spam parameter writes
    if (v > p.value + 1 || v + 1 < p.value || v == 0 || v == 127) p.value = v;
    return p.value;
  }

  uint8_t paramValue(uint8_t pot, uint8_t t) {
    Track& tr = seq.data.tracks[t];
    bool drum = seq.isDrumTrack(t);
    if (pot == 0) return drum ? tr.revSend : tr.cutoff;
    return drum ? tr.vol : tr.resonance;
  }

  void paramWrite(uint8_t pot, uint8_t t, uint8_t v) {
    bool drum = seq.isDrumTrack(t);
    if (pot == 0) { if (drum) seq.potSetRevSend(t, v); else seq.potSetCutoff(t, v); }
    else          { if (drum) seq.potSetVol(t, v);     else seq.potSetResonance(t, v); }
  }
}

namespace Pots {

void begin() {
  analogReadResolution(12);
  for (int i = 0; i < 2; i++) pots[i] = { 0, 0, 255, false, false };
}

void unlatch() {
  pots[0].latched = false;
  pots[1].latched = false;
}

uint8_t update(uint8_t currentTrack) {
  uint8_t wrote = 0;
  for (uint8_t i = 0; i < 2; i++) {
    Pot& p = pots[i];
    uint8_t prev = p.value;
    uint8_t v = readPot(i);
    uint8_t cur = paramValue(i, currentTrack);

    if (!p.latched) {
      // Re-latch when the knob crosses the parameter's stored value (or when
      // it parks hard at an end stop, which is an unambiguous intent).
      bool crossed = (prev <= cur && v >= cur) || (prev >= cur && v <= cur);
      bool moved   = (v > prev + 1) || (prev > v + 1);
      if ((moved && crossed) || (moved && (v == 0 || v == 127)))
        p.latched = true;
    }
    if (p.latched && v != p.lastSent) {
      paramWrite(i, currentTrack, v);
      p.lastSent = v;
      wrote |= (1u << i);
    }
  }
  return wrote;
}

const char* paramName(uint8_t pot, uint8_t currentTrack) {
  bool drum = seq.isDrumTrack(currentTrack);
  if (pot == 0) return drum ? "REV SEND" : "CUTOFF";
  return drum ? "VOLUME" : "RESONANCE";
}

uint8_t lastValue(uint8_t pot) { return pots[pot & 1].value; }
bool    latched(uint8_t pot)   { return pots[pot & 1].latched; }

} // namespace Pots

#else  // pots disabled: stubs

namespace Pots {
  void begin() {}
  uint8_t update(uint8_t) { return 0; }
  void unlatch() {}
  const char* paramName(uint8_t, uint8_t) { return ""; }
  uint8_t lastValue(uint8_t) { return 0; }
  bool latched(uint8_t) { return false; }
}

#endif
