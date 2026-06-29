// ============================================================================
//  GMSynth.cpp  -  GM voice backend, now backed by the internal SoundFont engine
//
//  Each function maps the old SAM2695 serial-MIDI call onto a SoundFont engine
//  queue call. The signatures and channel convention (1-based 1..16) are
//  unchanged, so Sequencer.cpp / UI.cpp compile and behave the same - only the
//  sound source moved from an external chip to an on-flash SF2 bank.
// ============================================================================
#include "GMSynth.h"
#include "SoundFont.h"

namespace {
  inline uint8_t clamp7(int v) { return (uint8_t)(v < 0 ? 0 : (v > 127 ? 127 : v)); }
}

namespace GMSynth {

void begin() {
  // The audio path (SoundFont::begin + AudioOut::begin) is brought up from the
  // sketch setup(); nothing to do here. Kept so existing call sites still link.
}

void noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  SoundFont::queueNoteOn(ch, clamp7(note), clamp7(vel));
}

void noteOff(uint8_t ch, uint8_t note) {
  SoundFont::queueNoteOff(ch, clamp7(note));
}

void controlChange(uint8_t ch, uint8_t cc, uint8_t value) {
  SoundFont::queueCC(ch, clamp7(cc), clamp7(value));
}

void programChange(uint8_t ch, uint8_t program) {
  SoundFont::queueProgram(ch, clamp7(program));
}

void pitchBend(uint8_t ch, int16_t bend14) {
  SoundFont::queuePitch(ch, bend14);
}

// ---- convenience wrappers (now plain CC / engine calls) --------------------
void bankSelect(uint8_t ch, uint8_t bankMSB) { SoundFont::queueBank(ch, bankMSB); }
void setVolume   (uint8_t ch, uint8_t v)     { controlChange(ch, 7,  v); }
void setPan      (uint8_t ch, uint8_t v)     { controlChange(ch, 10, v); }
void setExpression(uint8_t ch, uint8_t v)    { controlChange(ch, 11, v); }
void setModulation(uint8_t ch, uint8_t v)    { controlChange(ch, 1,  v); }
// Reverb/chorus sends + types: tsf has no global reverb/chorus, so these are
// accepted (and stored in the Song) but not yet audible. CC91/CC93 are passed
// through; tsf ignores unknown controllers safely. Kept so the MIX/FX UI works.
void setReverbSend(uint8_t ch, uint8_t v)    { controlChange(ch, 91, v); }
void setChorusSend(uint8_t ch, uint8_t v)    { controlChange(ch, 93, v); }
void setReverbType(uint8_t ch, uint8_t t)    { (void)ch; (void)t; }
void setChorusType(uint8_t ch, uint8_t t)    { (void)ch; (void)t; }

// ---- real-time / system ----------------------------------------------------
// No external gear to clock, so these do nothing now.
void clockTick() {}
void start()     {}
void stop()      {}
void cont()      {}

void allNotesOff(uint8_t ch) { SoundFont::queueAllOff(ch); }
void allSoundOff(uint8_t ch) { SoundFont::queueAllOff(ch); }
void panic()                 { SoundFont::queuePanic(); }

void gmReset()                { SoundFont::queueReset(); }
void masterVolume(uint8_t v)  { SoundFont::setMasterGain(clamp7(v)); }

} // namespace GMSynth
