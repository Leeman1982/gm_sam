// ============================================================================
//  GMSynth.cpp  -  Compatibility shim: the old external-GM driver API now
//                  drives the ON-CHIP SoundFont synth (Synth.*).
//
//  Keeping this API unchanged means the sequencer engine and UI did not have
//  to change at all -- the same noteOn/programChange/setVolume/... calls that
//  used to be serialised out the MIDI UART now poke the software voice engine.
//
//  Calls that only made sense for an outboard module (MIDI real-time clock /
//  transport bytes, reverb/chorus type & send -- there is no on-chip FX yet)
//  are accepted but do nothing, so callers stay simple.
// ============================================================================
#include "GMSynth.h"
#include "Synth.h"

namespace GMSynth {

void begin(){ Synth::begin(); }   // Audio::begin() is started separately in the .ino

void noteOn (uint8_t ch, uint8_t note, uint8_t vel){ Synth::noteOn(ch, note, vel); }
void noteOff(uint8_t ch, uint8_t note)             { Synth::noteOff(ch, note); }

void controlChange(uint8_t ch, uint8_t cc, uint8_t value){
  switch (cc) {
    case 1:   Synth::setExpression(ch, value); break;  // (mod -> treat softly)
    case 7:   Synth::setVolume(ch, value);     break;
    case 10:  Synth::setPan(ch, value);        break;
    case 11:  Synth::setExpression(ch, value); break;
    case 120: Synth::allSoundOff(ch);          break;
    case 123: Synth::allNotesOff(ch);          break;
    default: break;                                    // reverb/chorus etc: ignored
  }
}

void programChange(uint8_t ch, uint8_t program){ Synth::programChange(ch, program); }
void pitchBend    (uint8_t ch, int16_t bend14) { Synth::pitchBend(ch, bend14); }

void bankSelect   (uint8_t ch, uint8_t bankMSB){ Synth::bankSelect(ch, bankMSB); }
void setVolume    (uint8_t ch, uint8_t v)      { Synth::setVolume(ch, v); }
void setPan       (uint8_t ch, uint8_t v)      { Synth::setPan(ch, v); }
void setExpression(uint8_t ch, uint8_t v)      { Synth::setExpression(ch, v); }
void setModulation(uint8_t ch, uint8_t v)      { (void)ch; (void)v; }

// No on-chip reverb/chorus yet -- accepted, ignored.
void setReverbSend(uint8_t ch, uint8_t v){ (void)ch; (void)v; }
void setChorusSend(uint8_t ch, uint8_t v){ (void)ch; (void)v; }
void setReverbType(uint8_t ch, uint8_t t){ (void)ch; (void)t; }
void setChorusType(uint8_t ch, uint8_t t){ (void)ch; (void)t; }

// Transport / real-time bytes were for an external module; nothing to send now.
void clockTick(){}
void start()    {}
void stop()     {}
void cont()     {}

void allNotesOff(uint8_t ch){ Synth::allNotesOff(ch); }
void allSoundOff(uint8_t ch){ Synth::allSoundOff(ch); }
void panic()                { Synth::panic(); }

void gmReset()               { Synth::reset(); }
void masterVolume(uint8_t v) { Synth::masterVolume(v); }
void setFont(uint8_t index)  { Synth::setFont(index); }

} // namespace GMSynth
