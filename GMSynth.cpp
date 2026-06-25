// ============================================================================
//  GMSynth.cpp  -  Dream SAM2695 serial-MIDI driver implementation
// ============================================================================
#include "GMSynth.h"

namespace {
  inline uint8_t st(uint8_t status, uint8_t ch) {  // status nibble + channel(1..16)
    if (ch < 1)  ch = 1;
    if (ch > 16) ch = 16;
    return status | (uint8_t)(ch - 1);
  }
  inline uint8_t clamp7(int v) { return (uint8_t)(v < 0 ? 0 : (v > 127 ? 127 : v)); }

  // Single funnel for every byte that leaves on GP0, so the optional USB monitor
  // sees exactly what the SAM2695 sees. With MIDI_TX_DEBUG off this is a thin
  // inline wrapper over Serial1.write() and costs nothing.
  inline void tx(uint8_t b) {
    Serial1.write(b);
#if MIDI_TX_DEBUG
    if (b >= 0x80 && b < 0xF8) Serial.println();  // fresh line per channel message
    if (b < 0x10) Serial.print('0');
    Serial.print(b, HEX);
    Serial.print(' ');
#endif
  }
  inline void tx(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) tx(p[i]);
  }
}

namespace GMSynth {

volatile uint32_t notesSent = 0;   // diagnostic: counts Note-On messages sent

#if GM_MIDI_SELFTEST
// Audible power-on proof-of-life (toggle via GM_MIDI_SELFTEST in Config.h): a
// C-major arpeggio on channel 1 (piano = the GM default after reset). Hear it at
// boot -> the MIDI link to the SAM2695 is good and the issue is pattern/transport;
// no sound -> it is wiring / power / audio-out, not the firmware.
static void selfTest() {
  static const uint8_t kNotes[] = { 60, 64, 67, 72 };
  for (uint8_t i = 0; i < 4; i++) {
    noteOn(1, kNotes[i], 110);
    delay(180);
    noteOff(1, kNotes[i]);
    delay(40);
  }
}
#endif

void begin() {
  Serial1.setTX(PIN_MIDI_TX);
  Serial1.setRX(PIN_MIDI_RX);    // reserved for future external-sync input
  Serial1.setFIFOSize(256);      // generous TX/RX FIFO so writes never block
  Serial1.begin(31250);
#if MIDI_TX_DEBUG
  Serial.begin(115200);          // USB CDC: hex monitor of everything sent on GP0
#endif
  delay(60);                     // let the SAM2695 finish power-on
  gmReset();
  delay(20);
  masterVolume(120);
#if GM_MIDI_SELFTEST
  delay(20);
  selfTest();                    // disable by setting GM_MIDI_SELFTEST 0 in Config.h
#endif
}

void noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  tx(st(0x90, ch));
  tx(clamp7(note));
  tx(clamp7(vel));
  notesSent++;                     // drives the on-screen MIDI-activity dot
}

void noteOff(uint8_t ch, uint8_t note) {
  tx(st(0x80, ch));
  tx(clamp7(note));
  tx((uint8_t)0x40);             // release velocity 64
}

void controlChange(uint8_t ch, uint8_t cc, uint8_t value) {
  tx(st(0xB0, ch));
  tx(clamp7(cc));
  tx(clamp7(value));
}

void programChange(uint8_t ch, uint8_t program) {
  tx(st(0xC0, ch));
  tx(clamp7(program));
}

void pitchBend(uint8_t ch, int16_t bend14) {
  int v = bend14 + 8192;                 // 0..16383, centre 8192
  if (v < 0) v = 0; if (v > 16383) v = 16383;
  tx(st(0xE0, ch));
  tx((uint8_t)(v & 0x7F));               // LSB
  tx((uint8_t)((v >> 7) & 0x7F));        // MSB
}

void bankSelect(uint8_t ch, uint8_t bankMSB) { controlChange(ch, 0,  bankMSB); }
void setVolume   (uint8_t ch, uint8_t v)     { controlChange(ch, 7,  v); }
void setPan      (uint8_t ch, uint8_t v)     { controlChange(ch, 10, v); }
void setExpression(uint8_t ch, uint8_t v)    { controlChange(ch, 11, v); }
void setModulation(uint8_t ch, uint8_t v)    { controlChange(ch, 1,  v); }
void setReverbSend(uint8_t ch, uint8_t v)    { controlChange(ch, 91, v); }   // CC0x5B
void setChorusSend(uint8_t ch, uint8_t v)    { controlChange(ch, 93, v); }   // CC0x5D
void setReverbType(uint8_t ch, uint8_t t)    { controlChange(ch, 80, t & 7); } // CC0x50
void setChorusType(uint8_t ch, uint8_t t)    { controlChange(ch, 81, t & 7); } // CC0x51

void clockTick() { tx((uint8_t)0xF8); }
void start()     { tx((uint8_t)0xFA); }
void stop()      { tx((uint8_t)0xFC); }
void cont()      { tx((uint8_t)0xFB); }

void allNotesOff(uint8_t ch) { controlChange(ch, 123, 0); }
void allSoundOff(uint8_t ch) { controlChange(ch, 120, 0); }

void panic() {
  for (uint8_t ch = 1; ch <= 16; ch++) { allSoundOff(ch); allNotesOff(ch); }
}

void gmReset() {
  static const uint8_t sx[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
  tx(sx, sizeof(sx));
}

void masterVolume(uint8_t v) {
  v = clamp7(v);
  uint8_t sx[] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, v, 0xF7 };
  tx(sx, sizeof(sx));
}

} // namespace GMSynth
