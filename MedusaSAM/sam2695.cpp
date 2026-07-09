// ============================================================================
//  Medusa SAM  --  sam2695.cpp
//  See sam2695.h for the parameter map.  All byte values are clamped to 7 bit.
// ============================================================================
#include "sam2695.h"

namespace {
  inline uint8_t st(uint8_t status, uint8_t ch) {   // status nibble + channel
    if (ch < 1)  ch = 1;
    if (ch > 16) ch = 16;
    return status | (uint8_t)(ch - 1);
  }
  inline uint8_t c7(int v) { return (uint8_t)(v < 0 ? 0 : (v > 127 ? 127 : v)); }

  // Roland checksum over address + data bytes: (128 - sum%128) & 0x7F.
  uint8_t rolandSum(const uint8_t* b, uint8_t n) {
    uint16_t s = 0;
    for (uint8_t i = 0; i < n; i++) s += b[i];
    return (uint8_t)((128 - (s & 0x7F)) & 0x7F);
  }
}

namespace SAM {

void begin() {
  Serial1.setTX(PIN_MIDI_TX);
  Serial1.setRX(PIN_MIDI_RX);       // reserved for future clock-sync input
  Serial1.setFIFOSize(256);         // deep TX FIFO so bursts never block
  Serial1.begin(MIDI_BAUD);
  delay(80);                        // SAM2695 power-on settle
  gmReset();
  delay(30);
}

int txFree() { return Serial1.availableForWrite(); }

// ─── channel voice ───────────────────────────────────────────────────────────
void noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  Serial1.write(st(0x90, ch)); Serial1.write(c7(note)); Serial1.write(c7(vel));
}
void noteOff(uint8_t ch, uint8_t note) {
  Serial1.write(st(0x80, ch)); Serial1.write(c7(note)); Serial1.write((uint8_t)0x40);
}
void cc(uint8_t ch, uint8_t num, uint8_t val) {
  Serial1.write(st(0xB0, ch)); Serial1.write(c7(num)); Serial1.write(c7(val));
}
void programChange(uint8_t ch, uint8_t program) {
  Serial1.write(st(0xC0, ch)); Serial1.write(c7(program));
}
void channelPressure(uint8_t ch, uint8_t v) {
  Serial1.write(st(0xD0, ch)); Serial1.write(c7(v));
}
void pitchBend(uint8_t ch, int16_t bend14) {
  int v = (int)bend14 + 8192;
  if (v < 0) v = 0;
  if (v > 16383) v = 16383;
  Serial1.write(st(0xE0, ch));
  Serial1.write((uint8_t)(v & 0x7F));
  Serial1.write((uint8_t)((v >> 7) & 0x7F));
}

// ─── standard controllers ────────────────────────────────────────────────────
void bankSelect(uint8_t ch, uint8_t b)        { cc(ch, 0,  b); }
void setVolume(uint8_t ch, uint8_t v)         { cc(ch, 7,  v); }
void setPan(uint8_t ch, uint8_t v)            { cc(ch, 10, v); }
void setExpression(uint8_t ch, uint8_t v)     { cc(ch, 11, v); }
void setModWheel(uint8_t ch, uint8_t v)       { cc(ch, 1,  v); }
void setSustain(uint8_t ch, bool on)          { cc(ch, 64, on ? 127 : 0); }
void setPortamento(uint8_t ch, bool on)       { cc(ch, 65, on ? 127 : 0); }
void setPortamentoTime(uint8_t ch, uint8_t v) { cc(ch, 5,  v); }
void setReverbSend(uint8_t ch, uint8_t v)     { cc(ch, 91, v); }
void setChorusSend(uint8_t ch, uint8_t v)     { cc(ch, 93, v); }
// CC80/81 select the effect PROGRAM; the effect itself is global, so channel 1
// is as good as any (the Dream datasheet routes these to the FX block).
void setReverbProgram(uint8_t t)              { cc(1, 80, t & 7); }
void setChorusProgram(uint8_t t)              { cc(1, 81, t & 7); }

// ─── RPN / NRPN ──────────────────────────────────────────────────────────────
void rpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val) {
  cc(ch, 101, msb); cc(ch, 100, lsb); cc(ch, 6, val);
  cc(ch, 101, 127); cc(ch, 100, 127);        // RPN null: park the pointer
}
void setBendRange(uint8_t ch, uint8_t semis)     { rpn(ch, 0, 0, semis > 24 ? 24 : semis); }
void setChannelFineTune(uint8_t ch, uint8_t v)   { rpn(ch, 0, 1, v); }
void setChannelCoarseTune(uint8_t ch, uint8_t v) { rpn(ch, 0, 2, v); }

void nrpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val) {
  cc(ch, 99, msb); cc(ch, 98, lsb); cc(ch, 6, val);
  cc(ch, 99, 127); cc(ch, 98, 127);          // NRPN null
}
void setVibratoRate (uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x08, v); }
void setVibratoDepth(uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x09, v); }
void setVibratoDelay(uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x0A, v); }
void setCutoff      (uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x20, v); }
void setResonance   (uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x21, v); }
void setEnvAttack   (uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x63, v); }
void setEnvDecay    (uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x64, v); }
void setEnvRelease  (uint8_t ch, uint8_t v) { nrpn(ch, 0x01, 0x66, v); }

void setDrumPitch (uint8_t note, uint8_t v) { nrpn(DRUM_CHANNEL, 0x18, note, v); }
void setDrumLevel (uint8_t note, uint8_t v) { nrpn(DRUM_CHANNEL, 0x1A, note, v); }
void setDrumPan   (uint8_t note, uint8_t v) { nrpn(DRUM_CHANNEL, 0x1C, note, v); }
void setDrumReverb(uint8_t note, uint8_t v) { nrpn(DRUM_CHANNEL, 0x1D, note, v); }
void setDrumChorus(uint8_t note, uint8_t v) { nrpn(DRUM_CHANNEL, 0x1E, note, v); }

// Dream-specific: 4-band EQ.  Gains at 37 00..03 (64 = 0 dB), band centre
// frequencies at 37 08..0B.  Channel-agnostic (the EQ sits on the master bus).
void setEqGain(uint8_t band, uint8_t v) { nrpn(1, 0x37, (uint8_t)(0x00 + (band & 3)), v); }
void setEqFreq(uint8_t band, uint8_t v) { nrpn(1, 0x37, (uint8_t)(0x08 + (band & 3)), v); }

// ─── GS SysEx ────────────────────────────────────────────────────────────────
void gsParam2(uint8_t a1, uint8_t a2, uint8_t a3, const uint8_t* data, uint8_t n) {
  uint8_t body[24];
  if (n > 16) n = 16;
  body[0] = a1; body[1] = a2; body[2] = a3;
  for (uint8_t i = 0; i < n; i++) body[3 + i] = data[i] & 0x7F;
  uint8_t sum = rolandSum(body, (uint8_t)(3 + n));
  Serial1.write((uint8_t)0xF0); Serial1.write((uint8_t)0x41);
  Serial1.write((uint8_t)0x10); Serial1.write((uint8_t)0x42);
  Serial1.write((uint8_t)0x12);
  Serial1.write(body, 3 + n);
  Serial1.write(sum);  Serial1.write((uint8_t)0xF7);
}
void gsParam(uint8_t a1, uint8_t a2, uint8_t a3, uint8_t val) {
  gsParam2(a1, a2, a3, &val, 1);
}

void setReverbCharacter(uint8_t v)     { gsParam(0x40, 0x01, 0x31, c7(v)); }
void setReverbPreLpf(uint8_t v)        { gsParam(0x40, 0x01, 0x32, v & 7); }
void setReverbLevel(uint8_t v)         { gsParam(0x40, 0x01, 0x33, c7(v)); }
void setReverbTime(uint8_t v)          { gsParam(0x40, 0x01, 0x34, c7(v)); }
void setReverbDelayFeedback(uint8_t v) { gsParam(0x40, 0x01, 0x35, c7(v)); }
void setChorusPreLpf(uint8_t v)        { gsParam(0x40, 0x01, 0x39, v & 7); }
void setChorusLevel(uint8_t v)         { gsParam(0x40, 0x01, 0x3A, c7(v)); }
void setChorusFeedback(uint8_t v)      { gsParam(0x40, 0x01, 0x3B, c7(v)); }
void setChorusDelay(uint8_t v)         { gsParam(0x40, 0x01, 0x3C, c7(v)); }
void setChorusRate(uint8_t v)          { gsParam(0x40, 0x01, 0x3D, c7(v)); }
void setChorusDepth(uint8_t v)         { gsParam(0x40, 0x01, 0x3E, c7(v)); }
void setChorusToReverb(uint8_t v)      { gsParam(0x40, 0x01, 0x3F, c7(v)); }

// Master tune: nibble-encoded 16-bit value at 40 00 00, centre 0x0400 = A440,
// 0.1-cent units (0x0018 = -100.0 cents .. 0x07E8 = +100.0 cents).
void setMasterTuneCents(int cents) {
  if (cents < -100) cents = -100;
  if (cents >  100) cents =  100;
  uint16_t v = (uint16_t)(0x0400 + cents * 10);
  uint8_t d[4] = { (uint8_t)((v >> 12) & 0xF), (uint8_t)((v >> 8) & 0xF),
                   (uint8_t)((v >> 4)  & 0xF), (uint8_t)(v & 0xF) };
  gsParam2(0x40, 0x00, 0x00, d, 4);
}
void setMasterKeyShift(int8_t semis) {
  if (semis < -24) semis = -24;
  if (semis >  24) semis =  24;
  gsParam(0x40, 0x00, 0x05, (uint8_t)(0x40 + semis));
}
void setMasterPanGS(uint8_t v) { gsParam(0x40, 0x00, 0x06, c7(v == 0 ? 1 : v)); }

// GS part blocks: channel 10 -> block 0, channels 1-9 -> blocks 1-9,
// channels 11-16 -> blocks A-F.  mode: 0 = melodic, 1/2 = drum maps.
void setRhythmPart(uint8_t ch, uint8_t mode) {
  uint8_t block;
  if (ch == 10)     block = 0x0;
  else if (ch < 10) block = ch;
  else              block = ch - 1;
  gsParam(0x40, (uint8_t)(0x10 | block), 0x15, mode > 2 ? 2 : mode);
}

// ─── universal / realtime ───────────────────────────────────────────────────
void masterVolume(uint8_t v) {
  const uint8_t sx[] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, c7(v), 0xF7 };
  Serial1.write(sx, sizeof(sx));
}
void gmReset() {
  static const uint8_t sx[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
  Serial1.write(sx, sizeof(sx));
}
void gm2On() {
  static const uint8_t sx[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7 };
  Serial1.write(sx, sizeof(sx));
}
void gsReset() {
  static const uint8_t sx[] = { 0xF0, 0x41, 0x10, 0x42, 0x12,
                                0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
  Serial1.write(sx, sizeof(sx));
}
void sysex(const uint8_t* buf, uint16_t n) { Serial1.write(buf, n); }

void clockTick() { Serial1.write((uint8_t)0xF8); }
void start()     { Serial1.write((uint8_t)0xFA); }
void cont()      { Serial1.write((uint8_t)0xFB); }
void stop()      { Serial1.write((uint8_t)0xFC); }

void allNotesOff(uint8_t ch)         { cc(ch, 123, 0); }
void allSoundOff(uint8_t ch)         { cc(ch, 120, 0); }
void resetAllControllers(uint8_t ch) { cc(ch, 121, 0); }
void panic() {
  for (uint8_t ch = 1; ch <= 16; ch++) { allSoundOff(ch); allNotesOff(ch); }
}

} // namespace SAM
