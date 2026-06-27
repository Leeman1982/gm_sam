// ============================================================================
//  GMSynth.cpp  -  VS1053B GM-synth driver (SPI / real-time MIDI)
//
//  Brings the VS1053B up in real-time MIDI mode via the software start patch,
//  then streams 0x00-padded MIDI bytes over the SDI (XDCS) data bus. The public
//  API matches the previous SAM2695 UART driver, so the sequencer/UI are
//  unchanged. Only core1 (the engine) may call any of this -- it owns the SPI
//  bus; core0 only touches I2C (OLED) and flash (storage).
// ============================================================================
#include "GMSynth.h"
#include <SPI.h>

namespace {
  // ---- SCI registers / opcodes -----------------------------------------------
  constexpr uint8_t VS_WRITE     = 0x02;
  constexpr uint8_t VS_READ      = 0x03;
  constexpr uint8_t SCI_MODE     = 0x00;   // SM_SDINEW = 0x0800 (new-mode SDI)
  constexpr uint8_t SCI_STATUS   = 0x01;   // SS_VER in bits 7..4 (4 = VS1053)
  constexpr uint8_t SCI_BASS     = 0x02;
  constexpr uint8_t SCI_CLOCKF   = 0x03;
  constexpr uint8_t SCI_AUDATA   = 0x05;
  constexpr uint8_t SCI_VOL      = 0x0B;

  inline uint8_t st(uint8_t status, uint8_t ch) {  // status nibble + channel(1..16)
    if (ch < 1)  ch = 1;
    if (ch > 16) ch = 16;
    return status | (uint8_t)(ch - 1);
  }
  inline uint8_t clamp7(int v) { return (uint8_t)(v < 0 ? 0 : (v > 127 ? 127 : v)); }

  // DREQ is the chip's "ready" handshake; it drops during every SCI/SDI op. We
  // wait for it to rise before the next op, but with a guard so a missing/dead
  // module can never wedge the real-time engine on core1.
  inline void waitDreq() {
    uint32_t t0 = micros();
    while (!digitalRead(PIN_VS_DREQ)) {
      if ((uint32_t)(micros() - t0) > 100000UL) break;   // 100 ms safety timeout
    }
  }

  // ---- SCI (command) interface: 16-bit register read/write over XCS ----------
  // Always run at the slow init clock: that is within CLKI/7 (read) / CLKI/4
  // (write) even at the 12.288 MHz boot clock, so it is safe before AND after
  // SCI_CLOCKF is raised. SCI traffic is rare (master volume), so speed is moot.
  void sciWrite(uint8_t reg, uint16_t val) {
    waitDreq();
    SPI.beginTransaction(SPISettings(VS_SPI_HZ_INIT, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_VS_XCS, LOW);
    SPI.transfer(VS_WRITE);
    SPI.transfer(reg);
    SPI.transfer((uint8_t)(val >> 8));
    SPI.transfer((uint8_t)(val & 0xFF));
    digitalWrite(PIN_VS_XCS, HIGH);
    SPI.endTransaction();
  }

  uint16_t sciRead(uint8_t reg) {
    waitDreq();
    SPI.beginTransaction(SPISettings(VS_SPI_HZ_INIT, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_VS_XCS, LOW);
    SPI.transfer(VS_READ);
    SPI.transfer(reg);
    uint16_t v = (uint16_t)SPI.transfer(0x00) << 8;
    v |= SPI.transfer(0x00);
    digitalWrite(PIN_VS_XCS, HIGH);
    SPI.endTransaction();
    return v;
  }

  // ---- "VS1053b Realtime MIDI Start" patch (vs1053b-rtmidistart, 28 words) ----
  // Loaded into instruction RAM over SCI; its final record writes 0x50 to
  // SCI_AIADDR which auto-starts real-time MIDI mode (no separate start needed).
  const uint16_t rtMidiStart[28] = {
    0x0007, 0x0001, 0x8050, 0x0006, 0x0014, 0x0030, 0x0715, 0xb080,
    0x3400, 0x0007, 0x9255, 0x3d00, 0x0024, 0x0030, 0x0295, 0x6890,
    0x3400, 0x0030, 0x0495, 0x3d00, 0x0024, 0x2908, 0x4d40, 0x0030,
    0x0200, 0x000a, 0x0001, 0x0050
  };

  // Standard VS10xx compressed-plugin loader: walk (addr, count[, value]) records;
  // high bit of count = RLE run (repeat one value), else copy `count` words.
  void applyPatch(const uint16_t* p, uint16_t len) {
    uint16_t i = 0;
    while (i < len) {
      uint8_t  addr = (uint8_t)p[i++];
      uint16_t n    = p[i++];
      if (n & 0x8000) {                      // RLE run
        n &= 0x7FFF;
        uint16_t val = p[i++];
        while (n--) sciWrite(addr, val);
      } else {                               // copy run
        while (n--) sciWrite(addr, p[i++]);
      }
    }
  }

  // ---- SDI (data) interface: real-time MIDI bytes over XDCS ------------------
  // Each MIDI byte is sent as a 0x00 pad byte followed by the byte itself (the
  // datasheet's "pad with 0xFF" is a known error -- 0x00 is correct).
  inline void sendMidiByte(uint8_t b) {
    SPI.transfer((uint8_t)0x00);
    SPI.transfer(b);
  }

  void talkMIDI(uint8_t cmd, uint8_t d1, uint8_t d2) {
    const uint8_t status = cmd & 0xF0;
    const bool oneData = (status == 0xC0 || status == 0xD0);  // PC / ChannelPressure
    waitDreq();
    SPI.beginTransaction(SPISettings(VS_SPI_HZ_RUN, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_VS_XDCS, LOW);            // select DATA (SDI), not SCI
    sendMidiByte(cmd);
    sendMidiByte(d1);
    if (!oneData) sendMidiByte(d2);
    digitalWrite(PIN_VS_XDCS, HIGH);
    SPI.endTransaction();
#if MIDI_TX_DEBUG
    if (cmd < 0x10) Serial.print('0');
    Serial.print(cmd, HEX); Serial.print(' ');
    if (d1 < 0x10) Serial.print('0');
    Serial.print(d1, HEX);
    if (!oneData) { Serial.print(' '); if (d2 < 0x10) Serial.print('0'); Serial.print(d2, HEX); }
    Serial.println();
#endif
  }
}

namespace GMSynth {

volatile uint32_t notesSent = 0;   // diagnostic: counts Note-On messages sent

// Boot read-back diagnostics (populated once in begin()): chip version from
// SCI_STATUS SS_VER (4 = VS1053, 3 = VS1003, 0/15 = no SPI reply) and SCI_AUDATA
// (0xAC45 once real-time MIDI is live). Exposed so the UI can show them too.
volatile uint16_t vsVersion = 0xFFFF;
volatile uint16_t vsAudata  = 0xFFFF;

#if GM_MIDI_SELFTEST
// Audible power-on proof-of-life (toggle via GM_MIDI_SELFTEST in Config.h): a
// C-major arpeggio on channel 1 (piano = the GM default). Hear it at boot -> the
// SPI link + patch + audio path are good and any silence is pattern/transport;
// no sound -> wiring / power / audio-out, not the firmware.
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
  pinMode(PIN_VS_XCS,    OUTPUT); digitalWrite(PIN_VS_XCS,    HIGH);
  pinMode(PIN_VS_XDCS,   OUTPUT); digitalWrite(PIN_VS_XDCS,   HIGH);
  pinMode(PIN_VS_XRESET, OUTPUT); digitalWrite(PIN_VS_XRESET, HIGH);
  pinMode(PIN_VS_DREQ,   INPUT);
#if (PIN_VS_XCARDCS >= 0)
  // De-select the module's on-board microSD: a floating/low CARD-CS lets the SD
  // card drive MISO and corrupt SCI/SDI -> the VS1053 never enters MIDI mode.
  pinMode(PIN_VS_XCARDCS, OUTPUT); digitalWrite(PIN_VS_XCARDCS, HIGH);
#endif

  SPI.setSCK(PIN_VS_SCK);
  SPI.setTX(PIN_VS_MOSI);
  SPI.setRX(PIN_VS_MISO);
  SPI.begin();
#if (GM_VS_DIAG || MIDI_TX_DEBUG)
  Serial.begin(115200);          // USB CDC: boot diagnostic / SDI MIDI monitor
#endif

  // 1. Hardware reset: XRESET low ~10 ms, then high; wait for DREQ to rise.
  digitalWrite(PIN_VS_XRESET, LOW);  delay(10);
  digitalWrite(PIN_VS_XRESET, HIGH);
  waitDreq();
  delay(5);

  // 1b. Force new-mode SDI (SM_SDINEW). This is the power-on default, but setting
  //     it explicitly guards against a board/clone that comes up otherwise and
  //     keeps our dedicated-XDCS data path valid (SM_SDISHARE must stay 0).
  sciWrite(SCI_MODE, 0x0800);
  waitDreq();

  // 2. Clock multiplier BEFORE loading the patch; wait for DREQ after CLOCKF.
  sciWrite(SCI_CLOCKF, VS_CLOCKF);
  waitDreq();
  delay(2);

  // 3. Clean output: moderate volume, bass/treble flat.
  sciWrite(SCI_VOL,  0x2020);
  sciWrite(SCI_BASS, 0x0000);

  // 4. Load the real-time MIDI start patch (self-starts via AIADDR write).
  applyPatch(rtMidiStart, 28);

  // 5. Read the chip back over SPI: chip version (SS_VER) + SCI_AUDATA. AUDATA
  //    reads 0xAC45 (44100 Hz stereo) once real-time MIDI is live. Stored for the
  //    UI and printed once when GM_VS_DIAG is on -- this is the decisive "is the
  //    SPI link + chip alive?" check when there is no sound.
  vsVersion = (uint16_t)((sciRead(SCI_STATUS) >> 4) & 0x0F);
  vsAudata  = sciRead(SCI_AUDATA);
#if (GM_VS_DIAG || MIDI_TX_DEBUG)
  Serial.print(F("VS1053 ver=")); Serial.print(vsVersion);
  Serial.print(F(" AUDATA=0x"));  Serial.print(vsAudata, HEX);
  Serial.println(vsAudata == 0xAC45 ? F("  (RT-MIDI OK)") : F("  (RT-MIDI FAIL)"));
#endif

  masterVolume(120);
#if GM_MIDI_SELFTEST
  delay(20);
  selfTest();                    // disable by setting GM_MIDI_SELFTEST 0 in Config.h
#endif
}

void noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  talkMIDI(st(0x90, ch), clamp7(note), clamp7(vel));
  notesSent++;                     // drives the on-screen MIDI-activity dot
}

void noteOff(uint8_t ch, uint8_t note) {
  talkMIDI(st(0x80, ch), clamp7(note), 0x40);   // release velocity 64
}

void controlChange(uint8_t ch, uint8_t cc, uint8_t value) {
  talkMIDI(st(0xB0, ch), clamp7(cc), clamp7(value));
}

void programChange(uint8_t ch, uint8_t program) {
  talkMIDI(st(0xC0, ch), clamp7(program), 0);   // 0xC0 = single data byte
}

void pitchBend(uint8_t ch, int16_t bend14) {
  int v = bend14 + 8192;                 // 0..16383, centre 8192
  if (v < 0) v = 0;
  if (v > 16383) v = 16383;
  talkMIDI(st(0xE0, ch), (uint8_t)(v & 0x7F), (uint8_t)((v >> 7) & 0x7F));
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

// Transport / clock: the VS1053 is an internal synth on SPI -- there is no MIDI
// OUT port for an external slave, so these have no consumer. Kept as no-ops to
// preserve the engine's call sites (and to avoid feeding the ROM parser).
void clockTick() {}
void start()     {}
void stop()      {}
void cont()      {}

void allNotesOff(uint8_t ch) { controlChange(ch, 123, 0); }
void allSoundOff(uint8_t ch) { controlChange(ch, 120, 0); }

void panic() {
  for (uint8_t ch = 1; ch <= 16; ch++) { allSoundOff(ch); allNotesOff(ch); }
}

void gmReset() {
  // The VS1053 ROM MIDI parser does NOT skip SysEx, so we must not send the GM-On
  // SysEx. Emulate a reset with channel messages: silence everything and reset
  // controllers on all 16 channels. The engine follows this with reqResendAll,
  // which re-pushes bank/program/volume/pan/sends for a full effective GM reset.
  for (uint8_t ch = 1; ch <= 16; ch++) {
    allSoundOff(ch);
    allNotesOff(ch);
    controlChange(ch, 121, 0);   // CC121 = reset all controllers
  }
}

void masterVolume(uint8_t v) {
  v = clamp7(v);
  // Native VS1053 master volume via SCI_VOL: 0x00 = loudest, larger = quieter in
  // 0.5 dB steps (same attenuation on both channels). Map MIDI 0..127 so 127 is
  // loudest (atten 0) and 0 is near-silent (atten 254).
  uint8_t atten = (uint8_t)((127 - v) * 2);
  sciWrite(SCI_VOL, ((uint16_t)atten << 8) | atten);
}

} // namespace GMSynth
