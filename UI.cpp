// ============================================================================
//  UI.cpp  -  SSD1309 OLED rendering + input handling (core0)
// ============================================================================
#include "UI.h"
#include <Wire.h>
#include <U8g2lib.h>
#include "Config.h"
#include "Sequencer.h"
#include "Controls.h"
#include "Storage.h"
#include "GMNames.h"
#include "GMSynth.h"

// ---- Display object (SSD1309 2.42" 128x64, hardware I2C, full frame buffer) -
// Panel: 2.42" 128x64 OLED, SSD1309 controller, I2C @ 0x3C (U8g2, HW I2C / I2C0).
// If the image is shifted or garbled, try the NONAME2 init, or the SSD1306
// constructor (SSD1309 is largely register-compatible with SSD1306):
//   U8G2_SSD1309_128X64_NONAME2_F_HW_I2C  /  U8G2_SSD1306_128X64_NONAME_F_HW_I2C
static U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

namespace UI {

// ---------------------------------------------------------------------------
//  UI state (core0 only)
// ---------------------------------------------------------------------------
static uint8_t currentPage  = PAGE_SEQ;
static uint8_t currentTrack = 0;          // 0..MAX_TRACKS-1
static uint8_t seqCursor    = 0;          // step cursor on the SEQ page

// One cursor per list page (INST / MIX / FX / SONG).
static uint8_t instCursor = 0;            // 0..4
static uint8_t mixCursor  = 0;            // 0..3
static uint8_t fxCursor   = 0;            // 0..2
static uint8_t songCursor = 0;            // 0..7
static uint8_t slotSel    = 0;            // 0..NUM_SONG_SLOTS-1
static bool    editing    = false;        // list pages: click toggles edit-mode on the selected row

// Toast overlay.
static char     toastMsg[22] = {0};
static uint32_t toastUntil   = 0;

static void toast(const char* m) {
  strncpy(toastMsg, m, sizeof(toastMsg) - 1);
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toastUntil = millis() + 1100;
}

// ---------------------------------------------------------------------------
//  Small label tables
// ---------------------------------------------------------------------------
static const char* const kPageShort[PAGE_COUNT] = {
  "SEQ", "INST", "MIX", "FX", "SONG"
};
static const char* const kFieldName[SF_COUNT] = {
  "NOTE", "VEL", "GATE", "PROB", "MICRO"
};
static const char* const kRevName[8] = {
  "Room1","Room2","Room3","Hall1","Hall2","Plate","Delay","PanDly"
};
static const char* const kChoName[8] = {
  "Chor1","Chor2","Chor3","Chor4","FBChor","Flangr","ShrtDl","FB Dly"
};

// SAM2695 / GM drum kit names by program number (sparse; falls back to "Kit n").
static void drumKitName(uint8_t prog, char* buf, size_t n) {
  switch (prog) {
    case 0:  strncpy(buf, "Standard", n); break;
    case 8:  strncpy(buf, "Room",     n); break;
    case 16: strncpy(buf, "Power",    n); break;
    case 24: strncpy(buf, "Electro",  n); break;
    case 25: strncpy(buf, "TR-808",   n); break;
    case 32: strncpy(buf, "Jazz",     n); break;
    case 40: strncpy(buf, "Brush",    n); break;
    case 48: strncpy(buf, "Orchestr", n); break;
    case 56: strncpy(buf, "SFX",      n); break;
    default: snprintf(buf, n, "Kit %d", prog); break;
  }
  buf[n - 1] = '\0';
}

// ---------------------------------------------------------------------------
//  Cursor housekeeping
// ---------------------------------------------------------------------------
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void clampCursors() {
  uint8_t len = seq.data.tracks[currentTrack].length;
  if (seqCursor >= len) seqCursor = len ? len - 1 : 0;
}

static void nextPage() { currentPage = (currentPage + 1) % PAGE_COUNT; editing = false; }
static void prevPage() { currentPage = (currentPage + PAGE_COUNT - 1) % PAGE_COUNT; editing = false; }
static void nextTrack(){ currentTrack = (currentTrack + 1) % MAX_TRACKS; clampCursors(); editing = false; }
static void prevTrack(){ currentTrack = (currentTrack + MAX_TRACKS - 1) % MAX_TRACKS; clampCursors(); editing = false; }

// Audition the relevant note for the current context.
static void auditionCurrent() {
  Track& tr = seq.data.tracks[currentTrack];
  uint8_t note;
  if (currentPage == PAGE_SEQ) note = tr.steps[seqCursor].note;
  else                          note = (tr.channel == DRUM_CHANNEL) ? 36 : 60;
  seq.audition(currentTrack, note);
}

// ---------------------------------------------------------------------------
//  INPUT  (called every core0 loop)
// ---------------------------------------------------------------------------
static void doLoad() {
  // Read the file into a local copy first; touch no shared state unless it succeeds.
  Song tmp;
  if (!Storage::load(slotSel, tmp)) {
    char m[22]; snprintf(m, sizeof(m), "EMPTY slot %d", slotSel + 1); toast(m);
    return;
  }
  seq.loadSong(tmp);         // stops transport, parks core1, then swaps the Song race-free
  clampCursors();
  char m[22]; snprintf(m, sizeof(m), "LOADED slot %d", slotSel + 1); toast(m);
}

static void handleRotate(int d, bool shift) {
  if (currentPage == PAGE_SEQ) {
    Track& tr = seq.data.tracks[currentTrack];
    if (shift) seq.editStepField(currentTrack, seqCursor, tr.stepField, d);
    else       seqCursor = (uint8_t)clampi(seqCursor + d, 0, tr.length - 1);
    return;
  }

  // List pages: rotate moves the cursor, unless we are editing the selected row
  // (entered via click) or SHIFT is held -- then rotate changes the value.
  const bool edit = editing || shift;
  switch (currentPage) {
    case PAGE_INST:
      if (!edit) { instCursor = (uint8_t)clampi(instCursor + d, 0, 4); break; }
      switch (instCursor) {
        case 0: seq.setChannel(currentTrack, d); clampCursors(); break;
        case 1: seq.setProgram(currentTrack, d); break;
        case 2: seq.setBank(currentTrack);       break;   // toggle (any rotate)
        case 3: seq.setOctave(currentTrack, d);  break;
        case 4: seq.setLength(currentTrack, d);  clampCursors(); break;
      }
      break;

    case PAGE_MIX:
      if (!edit) { mixCursor = (uint8_t)clampi(mixCursor + d, 0, 3); break; }
      switch (mixCursor) {
        case 0: seq.setVol(currentTrack, d);     break;
        case 1: seq.setPan(currentTrack, d);     break;
        case 2: seq.setRevSend(currentTrack, d); break;
        case 3: seq.setChoSend(currentTrack, d); break;
      }
      break;

    case PAGE_FX:
      if (!edit) { fxCursor = (uint8_t)clampi(fxCursor + d, 0, 2); break; }
      switch (fxCursor) {
        case 0: seq.setRevType(d);   break;
        case 1: seq.setChoType(d);   break;
        case 2: seq.setMasterVol(d); break;
      }
      break;

    case PAGE_SONG:
      if (!edit) { songCursor = (uint8_t)clampi(songCursor + d, 0, 7); break; }
      switch (songCursor) {
        case 0: seq.setBpm(d);   break;
        case 1: seq.setSwing(d); break;
        case 2: seq.setSpb(d);   break;
        case 3: seq.data.clockSrc = (seq.data.clockSrc == CLK_INTERNAL)
                                    ? CLK_EXTERNAL : CLK_INTERNAL; break;
        case 4: slotSel = (uint8_t)clampi(slotSel + d, 0, NUM_SONG_SLOTS - 1); break;
        default: break;   // action rows have no value
      }
      break;
  }
}

// Run a SONG-page action row (Save / Load / GM Reset).
static void runSongAction() {
  switch (songCursor) {
    case 5: {                                         // Save
      char m[22];
      if (Storage::save(slotSel, seq.data))
           snprintf(m, sizeof(m), "SAVED slot %d", slotSel + 1);
      else snprintf(m, sizeof(m), "SAVE FAILED");
      toast(m);
    } break;
    case 6: doLoad(); break;                          // Load
    case 7: seq.gmReset(); toast("GM RESET SENT"); break;
    default: break;
  }
}

static void handleClick(bool shift) {
  if (currentPage == PAGE_SEQ) {
    Track& tr = seq.data.tracks[currentTrack];
    if (shift) {                              // cycle the per-step field
      tr.stepField = (tr.stepField + 1) % SF_COUNT;
      toast(kFieldName[tr.stepField]);
    } else {
      seq.toggleStep(currentTrack, seqCursor);
    }
    return;
  }

  // List pages: SONG action rows fire immediately; every value row toggles
  // edit-mode so a following rotate changes the value (no SHIFT needed).
  if (currentPage == PAGE_SONG && songCursor >= 5) { editing = false; runSongAction(); return; }
  editing = !editing;
}

void handleInput() {
  // Transport
  if (Controls::playPressed()) {
    if (Controls::shiftHeld()) seq.startTop(); else seq.play();
  }
  // Page navigation
  if (Controls::pagePressed()) {
    if (Controls::shiftHeld()) prevPage(); else nextPage();
  }
  // Track navigation / clear
  if (Controls::trackLongPress()) {
    seq.clearTrack(currentTrack);
    seqCursor = 0;
    toast("TRACK CLEARED");
  } else if (Controls::trackPressed()) {
    if (Controls::shiftHeld()) prevTrack(); else nextTrack();
  }
  // Mute / solo
  if (Controls::mutePressed()) {
    if (Controls::shiftHeld()) {
      seq.toggleSolo(currentTrack);
      toast(seq.data.tracks[currentTrack].solo ? "SOLO ON" : "SOLO OFF");
    } else {
      seq.toggleMute(currentTrack);
      toast(seq.data.tracks[currentTrack].mute ? "MUTED" : "UNMUTED");
    }
  }
  // Encoder long-press = audition
  if (Controls::encLongPress()) auditionCurrent();
  // Encoder rotate
  int d = Controls::encDelta();
  if (d != 0) handleRotate(d, Controls::shiftHeld());
  // Encoder click
  if (Controls::encClick()) handleClick(Controls::shiftHeld());
}

// ---------------------------------------------------------------------------
//  RENDER helpers
// ---------------------------------------------------------------------------
static void drawStatusBar() {
  Track& tr = seq.data.tracks[currentTrack];
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[24];

  // left: page + track + channel
  snprintf(buf, sizeof(buf), "%-4s T%02d C%02d", kPageShort[currentPage],
           currentTrack + 1, tr.channel);
  u8g2.drawStr(0, 7, buf);

  // mute / solo flag
  if (tr.solo)      u8g2.drawStr(78, 7, "S");
  else if (tr.mute) u8g2.drawStr(78, 7, "M");

  // right: transport + bpm
  if (seq.running()) u8g2.drawBox(88, 1, 6, 6);          // play = filled square
  else               u8g2.drawFrame(88, 1, 6, 6);        // stop = hollow square
  snprintf(buf, sizeof(buf), "%3d", seq.data.bpm);
  u8g2.drawStr(108, 7, buf);

  // MIDI TX activity: this dot pulses each time a Note-On is sent to the synth.
  // If it blinks while you play but you hear nothing, MIDI is leaving the Pico
  // correctly -- the fault is then the S wire / level or the audio out, not code.
  static uint32_t seenNotes = 0, pulseUntil = 0;
  uint32_t ns = GMSynth::notesSent;
  if (ns != seenNotes) { seenNotes = ns; pulseUntil = millis() + 150; }
  if (millis() < pulseUntil) u8g2.drawDisc(99, 4, 2);

  u8g2.drawHLine(0, 9, 128);
}

// Generic list-page row.
struct Row { char label[12]; char value[16]; };

static void drawRows(Row* rows, int n, int cursor, bool editSel) {
  const int listTop = 12, rowH = 10, visible = 5;
  int first = 0;
  if (n > visible) {
    first = cursor - visible / 2;
    if (first < 0) first = 0;
    if (first > n - visible) first = n - visible;
  }
  u8g2.setFont(u8g2_font_6x10_tr);
  for (int i = first; i < n && i < first + visible; ++i) {
    int top = listTop + (i - first) * rowH;
    bool sel = (i == cursor);
    if (sel) { u8g2.drawBox(0, top, 128, rowH); u8g2.setDrawColor(0); }
    u8g2.drawStr(2, top + 8, rows[i].label);
    // The selected row shows [value] while it is being edited, so it is obvious
    // that rotating now changes the value instead of moving the cursor.
    char out[20];
    if (sel && editSel && rows[i].value[0])
         snprintf(out, sizeof(out), "[%s]", rows[i].value);
    else snprintf(out, sizeof(out), "%s", rows[i].value);
    int vw = (int)strlen(out) * 6;
    u8g2.drawStr(126 - vw, top + 8, out);
    if (sel) u8g2.setDrawColor(1);
  }
}

// ---- per page renderers ----------------------------------------------------
static void renderSeq() {
  Track& tr = seq.data.tracks[currentTrack];
  const int gridTop = 13, pitch = 11, cw = 7;

  for (int s = 0; s < tr.length; ++s) {
    int col = s & 15, row = s >> 4;
    int x = col * 8, y = gridTop + row * pitch;
    if (tr.steps[s].active) u8g2.drawBox(x, y, cw, cw);     // solid = active
    else                    u8g2.drawFrame(x, y, cw, cw);   // hollow = empty
    if (s == seqCursor)     u8g2.drawFrame(x - 1, y - 1, cw + 2, cw + 2); // cursor halo
  }
  // playhead (XOR so it shows over any cell state)
  if (seq.running()) {
    int s = tr.curStep;
    if (s < tr.length) {
      int x = (s & 15) * 8, y = gridTop + (s >> 4) * pitch;
      u8g2.setDrawColor(2); u8g2.drawBox(x, y, cw, cw); u8g2.setDrawColor(1);
    }
  }

  // info line
  Step& sp = tr.steps[seqCursor];
  char name[10];
  if (tr.channel == DRUM_CHANNEL) gmDrumName(sp.note, name, sizeof(name));
  else                            noteName(sp.note, name, sizeof(name));
  name[7] = '\0';                                            // keep it short

  char val[14];
  switch (tr.stepField) {
    case SF_NOTE:  snprintf(val, sizeof(val), "[NOTE]%d", sp.note);  break;
    case SF_VEL:   snprintf(val, sizeof(val), "[VEL]%d",  sp.vel);   break;
    case SF_GATE:  snprintf(val, sizeof(val), "[GATE]%d", sp.gate);  break;
    case SF_PROB:  snprintf(val, sizeof(val), "[PRB]%d",  sp.prob);  break;
    case SF_MICRO: snprintf(val, sizeof(val), "[MIC]%+d", sp.micro); break;
  }

  u8g2.setFont(u8g2_font_5x7_tr);
  char left[10];
  snprintf(left, sizeof(left), "S%02d%s", seqCursor + 1, sp.active ? "*" : "");
  u8g2.drawStr(0, 63, left);
  u8g2.drawStr(34, 63, name);
  int vw = (int)strlen(val) * 5;
  u8g2.drawStr(127 - vw, 63, val);
}

static void renderInst() {
  Track& tr = seq.data.tracks[currentTrack];
  Row r[5];
  char nm[12];

  strcpy(r[0].label, "Channel");
  snprintf(r[0].value, sizeof(r[0].value), "%d%s", tr.channel,
           tr.channel == DRUM_CHANNEL ? " DRM" : "");

  strcpy(r[1].label, "Program");
  if (tr.channel == DRUM_CHANNEL) drumKitName(tr.program, nm, sizeof(nm));
  else                            gmInstrumentName(tr.program, nm, sizeof(nm));
  nm[11] = '\0';
  snprintf(r[1].value, sizeof(r[1].value), "%s", nm);

  strcpy(r[2].label, "Bank");
  strcpy(r[2].value, tr.bankMSB == 127 ? "MT-32" : "GM");

  strcpy(r[3].label, "Octave");
  snprintf(r[3].value, sizeof(r[3].value), "%+d", tr.octave);

  strcpy(r[4].label, "Length");
  snprintf(r[4].value, sizeof(r[4].value), "%d", tr.length);

  drawRows(r, 5, instCursor, editing);
}

static void renderMix() {
  Track& tr = seq.data.tracks[currentTrack];
  Row r[4];

  strcpy(r[0].label, "Volume");
  snprintf(r[0].value, sizeof(r[0].value), "%d", tr.vol);

  strcpy(r[1].label, "Pan");
  if (tr.pan == 64)      strcpy(r[1].value, "C");
  else if (tr.pan < 64)  snprintf(r[1].value, sizeof(r[1].value), "L%d", 64 - tr.pan);
  else                   snprintf(r[1].value, sizeof(r[1].value), "R%d", tr.pan - 64);

  strcpy(r[2].label, "Reverb Snd");
  snprintf(r[2].value, sizeof(r[2].value), "%d", tr.revSend);

  strcpy(r[3].label, "Chorus Snd");
  snprintf(r[3].value, sizeof(r[3].value), "%d", tr.choSend);

  drawRows(r, 4, mixCursor, editing);
}

static void renderFx() {
  Row r[3];
  strcpy(r[0].label, "Reverb");
  snprintf(r[0].value, sizeof(r[0].value), "%d %s", seq.data.revType,
           kRevName[seq.data.revType & 7]);
  strcpy(r[1].label, "Chorus");
  snprintf(r[1].value, sizeof(r[1].value), "%d %s", seq.data.choType,
           kChoName[seq.data.choType & 7]);
  strcpy(r[2].label, "Master Vol");
  snprintf(r[2].value, sizeof(r[2].value), "%d", seq.data.masterVol);
  drawRows(r, 3, fxCursor, editing);
}

static void renderSong() {
  Row r[8];
  strcpy(r[0].label, "BPM");
  snprintf(r[0].value, sizeof(r[0].value), "%d", seq.data.bpm);

  strcpy(r[1].label, "Swing");
  snprintf(r[1].value, sizeof(r[1].value), "%d%%", seq.data.swing);

  strcpy(r[2].label, "Resolution");
  snprintf(r[2].value, sizeof(r[2].value), "%d/beat", seq.stepsPerBeat());

  strcpy(r[3].label, "Clock");
  strcpy(r[3].value, seq.data.clockSrc == CLK_EXTERNAL ? "EXT" : "INT");

  strcpy(r[4].label, "Slot");
  snprintf(r[4].value, sizeof(r[4].value), "%d%s", slotSel + 1,
           Storage::exists(slotSel) ? " *" : "");

  strcpy(r[5].label, "> Save");      r[5].value[0] = '\0';
  strcpy(r[6].label, "> Load");      r[6].value[0] = '\0';
  strcpy(r[7].label, "> GM Reset");  r[7].value[0] = '\0';

  drawRows(r, 8, songCursor, editing);
}

static void drawToast() {
  if (millis() >= toastUntil) return;
  u8g2.setFont(u8g2_font_6x10_tr);
  int w = (int)strlen(toastMsg) * 6 + 8;
  int x = (128 - w) / 2; if (x < 0) x = 0;
  int y = 26;
  u8g2.setDrawColor(1); u8g2.drawBox(x, y, w, 14);
  u8g2.setDrawColor(0); u8g2.drawFrame(x, y, w, 14);
  u8g2.drawStr(x + 4, y + 10, toastMsg);
  u8g2.setDrawColor(1);
}

// ---------------------------------------------------------------------------
//  PUBLIC
// ---------------------------------------------------------------------------
void begin() {
  // I2C pins for the SSD1309 panel (must precede u8g2.begin()).
  Wire.setSDA(PIN_OLED_SDA);
  Wire.setSCL(PIN_OLED_SCL);
  Wire.begin();
  Wire.setClock(OLED_I2C_HZ);
  u8g2.begin();
  u8g2.setContrast(160);
}

// On-screen VS1053 bring-up check (for UF2 builds with no serial monitor).
// core1 reads the chip back over SPI early in GMSynth::begin(); we wait briefly
// for that, then show version + AUDATA so you can SEE whether real-time MIDI mode
// actually started. ver=4 + AUDATA=AC45 => SPI+chip+MIDI good (look at audio out);
// FAIL / garbage => SPI/reset wiring. Any button (or ~6 s) dismisses it.
void bootDiag() {
#if GM_VS_DIAG
  uint32_t t0 = millis();
  while (GMSynth::vsVersion == 0xFFFF && (millis() - t0) < 2000) delay(10);

  uint16_t ver = GMSynth::vsVersion;
  uint16_t aud = GMSynth::vsAudata;
  bool ok = (aud == 0xAC45);

  char line[24];
  snprintf(line, sizeof(line), "ver=%u  AUDATA=%04X", (unsigned)ver, (unsigned)aud);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 9, "VS1053 boot check");
  u8g2.drawHLine(0, 12, 128);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 24, line);
  u8g2.drawStr(0, 34, ok ? "RT-MIDI: OK" : "RT-MIDI: FAIL");
  if (ok) {
    u8g2.drawStr(0, 46, "SPI+chip OK -> check");
    u8g2.drawStr(0, 54, "audio out / gnd");
  } else {
    u8g2.drawStr(0, 46, "no SPI reply - check");
    u8g2.drawStr(0, 54, "MISO/XCS/XDCS/RESET");
  }
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 63, "any key to continue");
  u8g2.sendBuffer();

  uint32_t until = millis() + 6000;
  while (millis() < until) {
    Controls::update();
    if (Controls::encClick() || Controls::playPressed() || Controls::pagePressed()
        || Controls::trackPressed() || Controls::mutePressed()) break;
    delay(20);
  }
#endif
}

void render() {
  u8g2.clearBuffer();
  drawStatusBar();
  switch (currentPage) {
    case PAGE_SEQ:  renderSeq();  break;
    case PAGE_INST: renderInst(); break;
    case PAGE_MIX:  renderMix();  break;
    case PAGE_FX:   renderFx();   break;
    case PAGE_SONG: renderSong(); break;
  }
  drawToast();
  u8g2.sendBuffer();
}

} // namespace UI
