// ============================================================================
//  UI.cpp  -  SH1106 OLED rendering + input handling (core0)
// ============================================================================
#include "UI.h"
#include <Wire.h>
#include <U8g2lib.h>
#include "Config.h"
#include "Sequencer.h"
#include "Controls.h"
#include "Keypad.h"
#include "Pots.h"
#include "Storage.h"
#include "GMNames.h"
#include "Scales.h"

// ---- Display object (SH1106, hardware I2C, full frame buffer) --------------
static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

namespace UI {

// ---------------------------------------------------------------------------
//  UI state (core0 only)
// ---------------------------------------------------------------------------
static uint8_t currentPage  = PAGE_SEQ;
static uint8_t currentTrack = 0;          // 0..MAX_TRACKS-1
static uint8_t seqCursor    = 0;          // step cursor on the SEQ page
static bool    editMode     = false;      // menu pages: encoder edits the value

// Per-page menu row counts + cursors.
static const uint8_t kRowCount[PAGE_COUNT] = {
  0,  /* SEQ  */
  0,  /* PERF */
  10, /* TRACK */
  8,  /* MIX  */
  12, /* SYNTH */
  8,  /* FX   */
  5,  /* SCALE */
  9,  /* SONG */
};
static uint8_t menuCursor[PAGE_COUNT] = {0};
static uint8_t slotSel = 0;               // 0..NUM_SONG_SLOTS-1

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
  "SEQ", "PERF", "TRK", "MIX", "SYN", "FX", "SCL", "SONG"
};
static const char* const kFieldName[SF_COUNT] = {
  "NOTE", "VEL", "GATE", "PROB", "RTCH", "MICRO", "TIE"
};
static const char* const kRevName[8] = {
  "Room1","Room2","Room3","Hall1","Hall2","Plate","Delay","PanDly"
};
static const char* const kChoName[8] = {
  "Chor1","Chor2","Chor3","Chor4","FBChor","Flangr","ShrtDl","FB Dly"
};
static const char* const kDirName[DIR_COUNT] = { "FWD", "REV", "PNG", "RND" };

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

static void nextPage() { currentPage = (currentPage + 1) % PAGE_COUNT; editMode = false; }
static void prevPage() { currentPage = (currentPage + PAGE_COUNT - 1) % PAGE_COUNT; editMode = false; }

static void selectTrack(uint8_t t) {
  currentTrack = t % MAX_TRACKS;
  clampCursors();
  Pots::unlatch();                          // soft pickup: no parameter jumps
  char m[16]; snprintf(m, sizeof(m), "TRACK %d", currentTrack + 1);
  toast(m);
}
static void nextTrack(){ selectTrack((uint8_t)(currentTrack + 1)); }
static void prevTrack(){ selectTrack((uint8_t)(currentTrack + MAX_TRACKS - 1)); }

// Audition the relevant note for the current context.
static void auditionCurrent() {
  Track& tr = seq.data.tracks[currentTrack];
  uint8_t note;
  if (currentPage == PAGE_SEQ) note = tr.steps[seqCursor].note;
  else                         note = (tr.channel == DRUM_CHANNEL) ? 36 : 60;
  seq.audition(currentTrack, note);
}

// ---------------------------------------------------------------------------
//  MENU VALUE EDIT  (one switch per page keeps every parameter reachable)
// ---------------------------------------------------------------------------
static void doLoad() {
  seq.stop();
  uint32_t t0 = millis();
  while (seq.running() && (millis() - t0) < 80) delay(2);
  Song tmp;
  if (Storage::load(slotSel, tmp)) {
    seq.data = tmp;            // single memcpy; engine is stopped
    seq.resendAll();
    clampCursors();
    char m[22]; snprintf(m, sizeof(m), "LOADED slot %d", slotSel + 1); toast(m);
  } else {
    char m[22]; snprintf(m, sizeof(m), "EMPTY slot %d", slotSel + 1); toast(m);
  }
}

static void editValue(int d) {
  const uint8_t t = currentTrack;
  const uint8_t c = menuCursor[currentPage];
  switch (currentPage) {
    case PAGE_TRACK:
      switch (c) {
        case 0: seq.setChannel(t, d); break;
        case 1: seq.setProgram(t, d); break;
        case 2: seq.setBank(t);       break;   // toggle (any rotate)
        case 3: seq.setOctave(t, d);  break;
        case 4: seq.setTranspose(t, d); break;
        case 5: seq.setLength(t, d);  clampCursors(); break;
        case 6: seq.setDirection(t, d); break;
        case 7: seq.setClkDiv(t, d);  break;
        case 8: seq.setHumVel(t, d);  break;
        case 9: seq.setHumTime(t, d); break;
      }
      break;
    case PAGE_MIX:
      switch (c) {
        case 0: seq.setVol(t, d);        break;
        case 1: seq.setPan(t, d);        break;
        case 2: seq.setRevSend(t, d);    break;
        case 3: seq.setChoSend(t, d);    break;
        case 4: seq.setExpression(t, d); break;
        case 5: seq.setModulation(t, d); break;
        case 6: seq.toggleMute(t);       break;
        case 7: seq.toggleSolo(t);       break;
      }
      break;
    case PAGE_SYNTH:
      switch (c) {
        case 0:  seq.setCutoff(t, d);    break;
        case 1:  seq.setResonance(t, d); break;
        case 2:  seq.setAttack(t, d);    break;
        case 3:  seq.setDecay(t, d);     break;
        case 4:  seq.setRelease(t, d);   break;
        case 5:  seq.setVibRate(t, d);   break;
        case 6:  seq.setVibDepth(t, d);  break;
        case 7:  seq.setVibDelay(t, d);  break;
        case 8:  seq.setBendRange(t, d); break;
        case 9:  seq.togglePorta(t);     break;
        case 10: seq.setPortaTime(t, d); break;
        case 11: seq.toggleSustain(t);   break;
      }
      break;
    case PAGE_FX:
      switch (c) {
        case 0: seq.setRevType(d);   break;
        case 1: seq.setRevLevel(d);  break;
        case 2: seq.setRevTime(d);   break;
        case 3: seq.setChoType(d);   break;
        case 4: seq.setChoLevel(d);  break;
        case 5: seq.setChoRate(d);   break;
        case 6: seq.setChoDepth(d);  break;
        case 7: seq.setMasterVol(d); break;
      }
      break;
    case PAGE_SCALE:
      switch (c) {
        case 0: seq.setScaleRoot(d); break;
        case 1: seq.setScaleType(d); break;
        case 2: seq.setScaleLock(d); break;
        default: break;              // action rows ignore value edits
      }
      break;
    case PAGE_SONG:
      switch (c) {
        case 0: seq.setBpm(d);   break;
        case 1: seq.setSwing(d); break;
        case 2: seq.setSpb(d);   break;
        case 3: seq.data.clockSrc = (seq.data.clockSrc == CLK_INTERNAL)
                                    ? CLK_EXTERNAL : CLK_INTERNAL; break;
        case 4: slotSel = (uint8_t)clampi(slotSel + d, 0, NUM_SONG_SLOTS - 1); break;
        default: break;              // action rows ignore value edits
      }
      break;
    default: break;
  }
}

// Rows whose click runs an action instead of toggling edit mode.
static bool runAction() {
  const uint8_t c = menuCursor[currentPage];
  if (currentPage == PAGE_SCALE) {
    if (c == 3) { seq.quantizeTrack(currentTrack); toast("TRACK QUANTIZED"); return true; }
    if (c == 4) { seq.quantizeAll();               toast("ALL QUANTIZED");   return true; }
  }
  if (currentPage == PAGE_SONG) {
    if (c == 5) {
      char m[22];
      if (Storage::save(slotSel, seq.data))
           snprintf(m, sizeof(m), "SAVED slot %d", slotSel + 1);
      else snprintf(m, sizeof(m), "SAVE FAILED");
      toast(m);
      return true;
    }
    if (c == 6) { doLoad(); return true; }
    if (c == 7) { seq.gmReset(); toast("GM RESET SENT"); return true; }
    if (c == 8) { seq.panic();   toast("PANIC");         return true; }
  }
  return false;
}

// ---------------------------------------------------------------------------
//  INPUT  (called every core0 loop)
// ---------------------------------------------------------------------------
static void handleRotate(int d, bool shift) {
  Track& tr = seq.data.tracks[currentTrack];

  if (currentPage == PAGE_SEQ) {
    // hold a step key + rotate = edit that step directly (fast per-step edits)
    int held = Keypad::heldKey();
    if (held >= 0) {
      uint8_t s = (uint8_t)((seqCursor / 16) * 16 + held);
      if (s < tr.length) {
        seq.editStepField(currentTrack, s, tr.stepField, d);
        seqCursor = s;
      }
      Keypad::markActive();
      return;
    }
    if (shift) { seq.editStepField(currentTrack, seqCursor, tr.stepField, d);
                 Controls::suppressBack(); }
    else       seqCursor = (uint8_t)clampi(seqCursor + d, 0, tr.length - 1);
    return;
  }

  if (currentPage == PAGE_PERF) {
    currentTrack = (uint8_t)((currentTrack + MAX_TRACKS + d) % MAX_TRACKS);
    Pots::unlatch();
    return;
  }

  // menu pages
  if (shift)        { editValue(d); Controls::suppressBack(); }
  else if (editMode)  editValue(d);
  else menuCursor[currentPage] =
         (uint8_t)clampi(menuCursor[currentPage] + d, 0, kRowCount[currentPage] - 1);
}

static void handleClick(bool shift) {
  Track& tr = seq.data.tracks[currentTrack];
  if (shift) Controls::suppressBack();

  switch (currentPage) {
    case PAGE_SEQ:
      if (shift) {                              // cycle the per-step field
        tr.stepField = (tr.stepField + 1) % SF_COUNT;
        toast(kFieldName[tr.stepField]);
      } else {
        seq.toggleStep(currentTrack, seqCursor);
      }
      break;

    case PAGE_PERF:
      if (shift) {
        seq.toggleSolo(currentTrack);
        toast(tr.solo ? "SOLO ON" : "SOLO OFF");
      } else {
        seq.toggleMute(currentTrack);
        toast(tr.mute ? "MUTED" : "UNMUTED");
      }
      break;

    default:                                    // menu pages
      if (runAction()) break;
      editMode = !editMode;                     // click = enter/exit value edit
      break;
  }
}

static void handleKeypad(bool shift) {
  for (uint8_t k = 0; k < 16; k++) {
    if (Keypad::tap(k)) {
      if (shift) {                              // SHIFT+key = pick track (anywhere)
        selectTrack(k);
        Controls::suppressBack();
        continue;
      }
      switch (currentPage) {
        case PAGE_SEQ: {
          Track& tr = seq.data.tracks[currentTrack];
          uint8_t s = (uint8_t)((seqCursor / 16) * 16 + k);
          if (s < tr.length) { seq.toggleStep(currentTrack, s); seqCursor = s; }
          break;
        }
        case PAGE_PERF:
          seq.toggleMute(k);
          break;
        default:
          selectTrack(k);
          break;
      }
    }
    if (Keypad::longPress(k)) {
      switch (currentPage) {
        case PAGE_SEQ: {
          Track& tr = seq.data.tracks[currentTrack];
          uint8_t s = (uint8_t)((seqCursor / 16) * 16 + k);
          if (s < tr.length) { seqCursor = s; seq.audition(currentTrack, tr.steps[s].note); }
          break;
        }
        case PAGE_PERF:
          seq.toggleSolo(k);
          toast(seq.data.tracks[k].solo ? "SOLO ON" : "SOLO OFF");
          break;
        default:
          selectTrack(k);
          break;
      }
    }
  }
}

void handleInput() {
  bool shift = Controls::shiftHeld();

  // ---- macro pots (context-sensitive, soft pickup) ----
  uint8_t wrote = Pots::update(currentTrack);
  if (wrote) {
    uint8_t pot = (wrote & 1) ? 0 : 1;
    char m[22];
    snprintf(m, sizeof(m), "%s %d", Pots::paramName(pot, currentTrack),
             Pots::lastValue(pot));
    toast(m);
  }

  // ---- transport ----
  if (Controls::confirmPressed()) {
    if (shift) { seq.startTop(); Controls::suppressBack(); }
    else       seq.play();
  }
  if (Controls::confirmLong()) { seq.stop(); seq.panic(); toast("PANIC"); }

  // ---- BACK: exit edit mode, else next page ----
  if (Controls::backPressed()) {
    if (editMode) editMode = false;
    else          nextPage();
  }

  // ---- optional aux buttons ----
  if (Controls::pagePressed())  { if (shift) { prevPage(); Controls::suppressBack(); } else nextPage(); }
  if (Controls::trackLongPress()) {
    seq.clearTrack(currentTrack);
    seqCursor = 0;
    toast("TRACK CLEARED");
  } else if (Controls::trackPressed()) {
    if (shift) { prevTrack(); Controls::suppressBack(); } else nextTrack();
  }
  if (Controls::mutePressed()) {
    if (shift) {
      seq.toggleSolo(currentTrack); Controls::suppressBack();
      toast(seq.data.tracks[currentTrack].solo ? "SOLO ON" : "SOLO OFF");
    } else {
      seq.toggleMute(currentTrack);
      toast(seq.data.tracks[currentTrack].mute ? "MUTED" : "UNMUTED");
    }
  }

  // ---- keypad ----
  handleKeypad(shift);

  // ---- encoder ----
  if (Controls::encLongPress()) auditionCurrent();
  int d = Controls::encDelta();
  if (d != 0) handleRotate(d, shift);
  if (Controls::encClick()) handleClick(shift);
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

  // flags: mute/solo + scale lock
  if (tr.solo)      u8g2.drawStr(72, 7, "S");
  else if (tr.mute) u8g2.drawStr(72, 7, "M");
  if (seq.data.scaleLock != LOCK_OFF) u8g2.drawStr(79, 7, "L");

  // right: transport + bpm
  if (seq.running()) u8g2.drawBox(88, 1, 6, 6);          // play = filled square
  else               u8g2.drawFrame(88, 1, 6, 6);        // stop = hollow square
  snprintf(buf, sizeof(buf), "%3d", seq.data.bpm);
  u8g2.drawStr(108, 7, buf);

  u8g2.drawHLine(0, 9, 128);
}

// Generic list-page row.
struct Row { char label[12]; char value[16]; };

static void drawRows(Row* rows, int n, int cursor) {
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
    char vbuf[20];
    if (sel && editMode) snprintf(vbuf, sizeof(vbuf), ">%s<", rows[i].value);
    else                 snprintf(vbuf, sizeof(vbuf), "%s",  rows[i].value);
    int vw = (int)strlen(vbuf) * 6;
    u8g2.drawStr(126 - vw, top + 8, vbuf);
    if (sel) u8g2.setDrawColor(1);
  }
}

// value formatting helpers
static void fmtOnOff(char* v, size_t n, uint8_t on) { strncpy(v, on ? "ON" : "OFF", n); v[n-1] = 0; }
static void fmtBipolar(char* v, size_t n, uint8_t raw) { snprintf(v, n, "%+d", (int)raw - 64); }

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

  char val[14]; val[0] = '\0';
  switch (tr.stepField) {
    case SF_NOTE:    snprintf(val, sizeof(val), "[NOTE]%d",  sp.note);    break;
    case SF_VEL:     snprintf(val, sizeof(val), "[VEL]%d",   sp.vel);     break;
    case SF_GATE:    snprintf(val, sizeof(val), "[GATE]%d",  sp.gate);    break;
    case SF_PROB:    snprintf(val, sizeof(val), "[PRB]%d",   sp.prob);    break;
    case SF_RATCHET: snprintf(val, sizeof(val), "[RCH]x%d",  sp.ratchet); break;
    case SF_MICRO:   snprintf(val, sizeof(val), "[MIC]%+d",  sp.micro);   break;
    case SF_TIE:     snprintf(val, sizeof(val), "[TIE]%s",   sp.tie ? "ON" : "OFF"); break;
  }

  u8g2.setFont(u8g2_font_5x7_tr);
  char left[10];
  snprintf(left, sizeof(left), "S%02d%s", seqCursor + 1, sp.active ? "*" : "");
  u8g2.drawStr(0, 63, left);
  u8g2.drawStr(34, 63, name);
  int vw = (int)strlen(val) * 5;
  u8g2.drawStr(127 - vw, 63, val);
}

static void renderPerf() {
  // 4x4 grid mirroring the physical keypad: key k = track k.
  const int top = 12, cellW = 32, cellH = 13;
  u8g2.setFont(u8g2_font_5x7_tr);
  bool solos = seq.anySolo();
  for (uint8_t t = 0; t < MAX_TRACKS; t++) {
    int x = (t & 3) * cellW, y = top + (t >> 2) * cellH;
    Track& tr = seq.data.tracks[t];
    bool sel = (t == currentTrack);
    if (sel) u8g2.drawFrame(x, y, cellW - 1, cellH - 1);
    u8g2.drawFrame(x + 1, y + 1, cellW - 3, cellH - 3);
    char b[8];
    const char* flag = tr.solo ? "S" : (tr.mute ? "M" : (solos ? "-" : ""));
    snprintf(b, sizeof(b), "%d%s", t + 1, flag);
    u8g2.drawStr(x + 4, y + 9, b);
    // audible tracks get a filled dot on the right of the cell
    if (seq.trackAudible(t)) u8g2.drawBox(x + cellW - 8, y + 4, 4, 4);
  }
}

static void renderTrack() {
  Track& tr = seq.data.tracks[currentTrack];
  Row r[10];
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

  strcpy(r[4].label, "Transpose");
  snprintf(r[4].value, sizeof(r[4].value), "%+d st", tr.transpose);

  strcpy(r[5].label, "Length");
  snprintf(r[5].value, sizeof(r[5].value), "%d", tr.length);

  strcpy(r[6].label, "Direction");
  strcpy(r[6].value, kDirName[tr.direction % DIR_COUNT]);

  strcpy(r[7].label, "Clock Div");
  snprintf(r[7].value, sizeof(r[7].value), "/%d", kClockDivOptions[tr.clkDiv % NUM_DIV_OPTIONS]);

  strcpy(r[8].label, "Human Vel");
  snprintf(r[8].value, sizeof(r[8].value), "%d", tr.humVel);

  strcpy(r[9].label, "Human Time");
  snprintf(r[9].value, sizeof(r[9].value), "%d", tr.humTime);

  drawRows(r, 10, menuCursor[PAGE_TRACK]);
}

static void renderMix() {
  Track& tr = seq.data.tracks[currentTrack];
  Row r[8];

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

  strcpy(r[4].label, "Expression");
  snprintf(r[4].value, sizeof(r[4].value), "%d", tr.expression);

  strcpy(r[5].label, "Mod Wheel");
  snprintf(r[5].value, sizeof(r[5].value), "%d", tr.modulation);

  strcpy(r[6].label, "Mute");
  fmtOnOff(r[6].value, sizeof(r[6].value), tr.mute);

  strcpy(r[7].label, "Solo");
  fmtOnOff(r[7].value, sizeof(r[7].value), tr.solo);

  drawRows(r, 8, menuCursor[PAGE_MIX]);
}

static void renderSynth() {
  Track& tr = seq.data.tracks[currentTrack];
  Row r[12];

  strcpy(r[0].label, "Cutoff");     fmtBipolar(r[0].value, sizeof(r[0].value), tr.cutoff);
  strcpy(r[1].label, "Resonance");  fmtBipolar(r[1].value, sizeof(r[1].value), tr.resonance);
  strcpy(r[2].label, "Attack");     fmtBipolar(r[2].value, sizeof(r[2].value), tr.attack);
  strcpy(r[3].label, "Decay");      fmtBipolar(r[3].value, sizeof(r[3].value), tr.decay);
  strcpy(r[4].label, "Release");    fmtBipolar(r[4].value, sizeof(r[4].value), tr.release);
  strcpy(r[5].label, "Vib Rate");   fmtBipolar(r[5].value, sizeof(r[5].value), tr.vibRate);
  strcpy(r[6].label, "Vib Depth");  fmtBipolar(r[6].value, sizeof(r[6].value), tr.vibDepth);
  strcpy(r[7].label, "Vib Delay");  fmtBipolar(r[7].value, sizeof(r[7].value), tr.vibDelay);
  strcpy(r[8].label, "Bend Range");
  snprintf(r[8].value, sizeof(r[8].value), "%d st", tr.bendRange);
  strcpy(r[9].label, "Portamento"); fmtOnOff(r[9].value, sizeof(r[9].value), tr.portaOn);
  strcpy(r[10].label, "Porta Time");
  snprintf(r[10].value, sizeof(r[10].value), "%d", tr.portaTime);
  strcpy(r[11].label, "Sustain");   fmtOnOff(r[11].value, sizeof(r[11].value), tr.sustain);

  drawRows(r, 12, menuCursor[PAGE_SYNTH]);
}

static void renderFx() {
  Row r[8];
  strcpy(r[0].label, "Rev Type");
  snprintf(r[0].value, sizeof(r[0].value), "%d %s", seq.data.revType,
           kRevName[seq.data.revType & 7]);
  strcpy(r[1].label, "Rev Level");
  snprintf(r[1].value, sizeof(r[1].value), "%d", seq.data.revLevel);
  strcpy(r[2].label, "Rev Time");
  snprintf(r[2].value, sizeof(r[2].value), "%d", seq.data.revTime);
  strcpy(r[3].label, "Cho Type");
  snprintf(r[3].value, sizeof(r[3].value), "%d %s", seq.data.choType,
           kChoName[seq.data.choType & 7]);
  strcpy(r[4].label, "Cho Level");
  snprintf(r[4].value, sizeof(r[4].value), "%d", seq.data.choLevel);
  strcpy(r[5].label, "Cho Rate");
  snprintf(r[5].value, sizeof(r[5].value), "%d", seq.data.choRate);
  strcpy(r[6].label, "Cho Depth");
  snprintf(r[6].value, sizeof(r[6].value), "%d", seq.data.choDepth);
  strcpy(r[7].label, "Master Vol");
  snprintf(r[7].value, sizeof(r[7].value), "%d", seq.data.masterVol);
  drawRows(r, 8, menuCursor[PAGE_FX]);
}

static void renderScale() {
  Row r[5];
  strcpy(r[0].label, "Root");
  strcpy(r[0].value, kRootNames[seq.data.scaleRoot % 12]);
  strcpy(r[1].label, "Scale");
  strcpy(r[1].value, kScales[seq.data.scaleType % NUM_SCALES].name);
  strcpy(r[2].label, "Lock");
  fmtOnOff(r[2].value, sizeof(r[2].value), seq.data.scaleLock != LOCK_OFF);
  strcpy(r[3].label, "> Quant Trk"); r[3].value[0] = '\0';
  strcpy(r[4].label, "> Quant All"); r[4].value[0] = '\0';
  drawRows(r, 5, menuCursor[PAGE_SCALE]);
}

static void renderSong() {
  Row r[9];
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
  strcpy(r[8].label, "> Panic");     r[8].value[0] = '\0';

  drawRows(r, 9, menuCursor[PAGE_SONG]);
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
  // I2C pins per the tested SH1106 setup (must precede u8g2.begin()).
  Wire.setSDA(PIN_OLED_SDA);
  Wire.setSCL(PIN_OLED_SCL);
  Wire.begin();
  Wire.setClock(OLED_I2C_HZ);
  u8g2.begin();
  u8g2.setContrast(160);
}

void render() {
  u8g2.clearBuffer();
  drawStatusBar();
  switch (currentPage) {
    case PAGE_SEQ:   renderSeq();   break;
    case PAGE_PERF:  renderPerf();  break;
    case PAGE_TRACK: renderTrack(); break;
    case PAGE_MIX:   renderMix();   break;
    case PAGE_SYNTH: renderSynth(); break;
    case PAGE_FX:    renderFx();    break;
    case PAGE_SCALE: renderScale(); break;
    case PAGE_SONG:  renderSong();  break;
  }
  drawToast();
  u8g2.sendBuffer();
}

} // namespace UI
