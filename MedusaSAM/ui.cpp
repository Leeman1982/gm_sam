// ============================================================================
//  Medusa SAM  --  ui.cpp  (core0)
// ============================================================================
#include "ui.h"
#include "gm_names.h"
#include <U8g2lib.h>
#include <Wire.h>

// 1.3" ST7567S COG LCD (EstarDyn GM12864-59N), hardware I2C, full frame buffer.
// The U8g2 driver profile is selected by ST7567_PROFILE in config.h -- the
// GM12864-59N needs the JLX12864 profile (not the plain DG128064 one, which
// sends valid I2C but never powers the panel bias -> permanently blank).
#if ST7567_PROFILE == 0
static U8G2_ST7567_JLX12864_F_HW_I2C      oled(U8G2_R0, U8X8_PIN_NONE);
#else
static U8G2_ST7567_ENH_DG128064I_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#endif

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static const char* const kViewNames[(int)View::NUM] =
    { "STEP", "INST", "SHAPE", "MIX", "FX", "MASTR", "XPRT", "SONG" };
static const char* const kStepFieldNames[SF_COUNT] =
    { "NOTE", "VEL", "GATE", "PROB", "MICR", "RTCH" };
static const char* const kModeNames[RM_NUM] = { "Auto", "Melodic", "Drums" };
static const char* const kDestNames[DEST_NUM] = { "SAM", "Layer" };

// ─── lifecycle ───────────────────────────────────────────────────────────────
void UI::begin(Song* song, Engine* eng, Controls* ctl) {
    _song = song; _eng = eng; _ctl = ctl;
    Wire.setSDA(PIN_OLED_SDA);
    Wire.setSCL(PIN_OLED_SCL);
    Wire.setClock(OLED_I2C_HZ);           // set before begin() -- matches the
    Wire.begin();                         // known-working RP2350 init order
    oled.setI2CAddress(OLED_ADDR << 1);   // U8g2 wants the 8-bit address
    oled.begin();
    oled.setBusClock(OLED_I2C_HZ);
    oled.setContrast(OLED_CONTRAST);      // ST7567S: too low a value = blank screen

    // Boot splash: proves the panel is wired before any sequencer state matters.
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);
    drawCentered(24, "MEDUSA  SAM");
    oled.setFont(u8g2_font_5x7_tr);
    drawCentered(40, "SAM2695 GM sequencer");
    drawCentered(52, "16trk x 64stp");
    oled.sendBuffer();
}

// Horizontally centers `s` (in the currently-set font) within the display's
// safe zone (SCREEN_L..SCREEN_R) -- the ST7567S COG clips/garbles the raw
// canvas edges, so nothing should be positioned by hand against 0/127.
void UI::drawCentered(int y, const char* s) {
    int w = oled.getStrWidth(s);
    int x = SCREEN_L + (SCREEN_W - w) / 2;
    if (x < SCREEN_L) x = SCREEN_L;
    oled.drawStr(x, y, s);
}

void UI::toast(const char* msg) {
    strncpy(_toast, msg, sizeof(_toast) - 1);
    _toast[sizeof(_toast) - 1] = 0;
    _toastUntil = millis() + 1100;
}

Pattern& UI::soundingPat() { return _song->pattern[_eng->playPat]; }

void UI::nextTrack(int d) {
    int t = (int)_track + d;
    while (t < 0) t += NUM_TRACKS;
    _track = (uint8_t)(t % NUM_TRACKS);
}

void UI::clearTrackRow() {
    Pattern& p = soundingPat();
    for (int i = 0; i < NUM_STEPS; i++) {
        p.step[_track][i].flags = 0;
        p.step[_track][i].ratchet = 1;
    }
    toast("track cleared");
}

const char* UI::trackLabel(uint8_t t, char* buf, size_t n) {
    const TrackCfg& c = _song->track[t];
    if (trackIsDrum(c)) {
        // name the lane after its first active drum note; else the kit
        const Pattern& p = _song->pattern[_eng->playPat];
        for (int s = 0; s < NUM_STEPS; s++)
            if (stepOn(p.step[t][s])) { gmDrumName(p.step[t][s].note, buf, n); return buf; }
        gmKitName(c.program, buf, n);
    } else {
        gmInstrumentName(c.program, buf, n);
    }
    return buf;
}

void UI::audition() {
    const TrackCfg& c = _song->track[_track];
    uint8_t note;
    if (trackIsDrum(c)) {
        note = soundingPat().step[_track][_cursor].note;
    } else {
        note = scaleSnap(_song->scaleRoot, _song->scaleType, 60);
        note = (uint8_t)clampi((int)note + (int)c.octave * 12, 0, 127);
    }
    _eng->audCh = c.channel; _eng->audNote = note; _eng->audVel = 110;
    _eng->audReq = 1;
}

// ─── save / load (engine parked + core1 locked out of flash) ────────────────
void UI::saveLoad(bool load) {
    _eng->reqStop  = 1;
    _eng->reqPause = 1;
    uint32_t t0 = millis();
    while (!_eng->paused && millis() - t0 < 1000) { delay(1); }
    bool ok = false;
    if (_eng->paused) {
        rp2040.idleOtherCore();
        ok = load ? Storage::load(_slot, *_song) : Storage::save(_slot, *_song);
        rp2040.resumeOtherCore();
    }
    _eng->reqPause = 0;          // engine resumes and queues a full resend
    if (load) toast(ok ? "loaded" : "empty slot");
    else      toast(ok ? "saved" : "save fail");
}

// ─── input dispatch ──────────────────────────────────────────────────────────
void UI::handleInput() {
    Controls& c = *_ctl;
    bool shift = c.shift.isHeld();
    int enc = c.encoder.getDelta();

    if (c.play.wasPressed()) {
        if (shift) { _eng->reqContinue = 1; }
        else if (_eng->running()) _eng->reqStop = 1;
        else                      _eng->reqStart = 1;
    }
    if (c.page.wasPressed()) {
        int v = (int)_view + (shift ? -1 : 1);
        v = (v + (int)View::NUM) % (int)View::NUM;
        _view = (View)v; _field = 0; _scroll = 0;
    }
    if (c.track.wasPressed())   nextTrack(shift ? -1 : 1);
    if (c.track.wasLongPress()) clearTrackRow();

    if (shift && enc) {          // tempo from any view
        _song->bpm = (uint16_t)clampi((int)_song->bpm + enc, BPM_MIN, BPM_MAX);
        enc = 0;
    }

    switch (_view) {
        case View::STEP: onStepView(enc, shift); break;
        case View::MIX:  onMixView(enc, shift);  break;
        default:         onListView(enc, shift); break;
    }
}

// ─── STEP grid view ──────────────────────────────────────────────────────────
void UI::onStepView(int enc, bool shift) {
    Controls& c = *_ctl;
    Pattern& p = soundingPat();
    uint8_t tl = (uint8_t)clampi(p.tlen[_track], 1, NUM_STEPS);
    if (_cursor >= tl) _cursor = tl - 1;
    Step& st = p.step[_track][_cursor];
    bool drum = trackIsDrum(_song->track[_track]);

    if (enc) {
        if (c.rec.isHeld()) {
            switch (_stepField) {
                case SF_NOTE:
                    if (drum) st.note = (uint8_t)clampi((int)st.note + enc, 27, 87);
                    else {
                        uint8_t n = st.note;
                        for (int k = 0; k < abs(enc); k++)
                            n = scaleStep(_song->scaleRoot, _song->scaleType, n,
                                          enc > 0 ? 1 : -1);
                        st.note = n;
                    }
                    break;
                case SF_VEL:   st.vel     = (uint8_t)clampi((int)st.vel + enc, 1, 127); break;
                case SF_GATE:  st.gate    = (uint8_t)clampi((int)st.gate + enc * 5, 1, 100); break;
                case SF_PROB:  st.prob    = (uint8_t)clampi((int)st.prob + enc * 5, 0, 100); break;
                case SF_MICRO: st.micro   = (int8_t) clampi((int)st.micro + enc, -12, 12); break;
                case SF_RATCH: st.ratchet = (uint8_t)clampi((int)st.ratchet + enc, 1, 4); break;
            }
            st.flags |= STEP_ON;
            _recEditing = true;              // suppress the accent tap
        } else {
            _cursor = (uint8_t)((_cursor + enc + tl * 4) % tl);
        }
    }
    if (c.encoder.wasPressed()) {
        if (shift) _stepField = (uint8_t)((_stepField + 1) % SF_COUNT);
        else       st.flags ^= STEP_ON;
    }
    if (c.encoder.wasLongPress()) clearTrackRow();
    if (c.rec.wasPressed()) {
        if (_recEditing) _recEditing = false;   // was an edit, not a tap
        else {
            if (shift) st.flags ^= STEP_TIE; else st.flags ^= STEP_ACCENT;
            st.flags |= STEP_ON;
        }
    }
}

// ─── MIX view ────────────────────────────────────────────────────────────────
void UI::onMixView(int enc, bool shift) {
    Controls& c = *_ctl;
    TrackCfg& cfg = _song->track[_track];
    if (enc) cfg.vol = (uint8_t)clampi((int)cfg.vol + enc * 4, 0, 127);
    if (c.encoder.wasPressed()) {
        if (shift) { cfg.solo ^= 1; toast(cfg.solo ? "solo" : "solo off"); }
        else       { cfg.mute ^= 1; toast(cfg.mute ? "muted" : "unmuted"); }
    }
    if (c.rec.wasPressed()) audition();
}

// ─── generic list views ──────────────────────────────────────────────────────
void UI::onListView(int enc, bool shift) {
    Controls& c = *_ctl;
    uint8_t count = listRowCount();
    if (_field >= count) _field = count - 1;

    if (c.encoder.wasPressed()) _field = (uint8_t)((_field + 1) % count);
    if (enc) listRowEdit(_field, enc);
    if (c.rec.wasPressed()) {
        if (listRowIsAction(_field)) listRowAction(_field);
        else                         audition();
    }
    // keep the cursor inside the 7-row window
    if (_field < _scroll) _scroll = _field;
    if (_field >= _scroll + 7) _scroll = (uint8_t)(_field - 6);
}

// row tables ------------------------------------------------------------------
enum { INST_ROWS = 12, SHAPE_ROWS = 11, FX_ROWS = 13, MASTER_ROWS = 16,
       XPRT_ROWS = 14 };
#define SONG_FIXED 8                       // rows before the chain entries
#define SONG_ROWS  (SONG_FIXED + CHAIN_LEN + 5)   // + Slot,Save,Load,Copy,Clear

uint8_t UI::listRowCount() {
    switch (_view) {
        case View::INST:   return INST_ROWS;
        case View::SHAPE:  return SHAPE_ROWS;
        case View::FX:     return FX_ROWS;
        case View::MASTER: return MASTER_ROWS;
        case View::XPRT:   return XPRT_ROWS;
        case View::SONG:   return SONG_ROWS;
        default:           return 1;
    }
}

bool UI::listRowIsAction(uint8_t row) {
    switch (_view) {
        case View::MASTER: return row >= 12;                  // resets / panic
        case View::XPRT:   return row == 3 || row == 8 || row == 13;
        case View::SONG:   return row >= SONG_FIXED + CHAIN_LEN + 1; // Save..Clear
        default:           return false;
    }
}

void UI::listRowEdit(uint8_t row, int enc) {
    TrackCfg& t = _song->track[_track];
    FxParams& f = _song->fx;
    Pattern&  ep = _song->pattern[_editPat];

    switch (_view) {
    case View::INST:
        switch (row) {
            case 0:  t.channel = (uint8_t)clampi((int)t.channel + enc, 1, 16); break;
            case 1:  t.rhythm  = (uint8_t)clampi((int)t.rhythm + enc, 0, RM_NUM - 1); break;
            case 2:  t.program = (uint8_t)clampi((int)t.program + enc, 0, 127); break;
            case 3:  t.bankMSB = (enc > 0) ? 127 : 0; break;
            case 4:  t.octave  = (int8_t)clampi((int)t.octave + enc, -3, 3); break;
            case 5:  t.vol     = (uint8_t)clampi((int)t.vol + enc * 2, 0, 127); break;
            case 6:  t.pan     = (uint8_t)clampi((int)t.pan + enc * 2, 0, 127); break;
            case 7:  t.revSend = (uint8_t)clampi((int)t.revSend + enc * 2, 0, 127); break;
            case 8:  t.choSend = (uint8_t)clampi((int)t.choSend + enc * 2, 0, 127); break;
            case 9:  t.bendRange = (uint8_t)clampi((int)t.bendRange + enc, 0, 24); break;
            case 10: t.dest    = (uint8_t)clampi((int)t.dest + enc, 0, DEST_NUM - 1); break;
            case 11: t.mute   ^= 1; break;
        }
        break;
    case View::SHAPE:
        switch (row) {
            case 0: {
                Pattern& sp = soundingPat();
                sp.tlen[_track] = (uint8_t)clampi((int)sp.tlen[_track] + enc, 1, NUM_STEPS);
                break;
            }
            case 1:  t.portaTime = (uint8_t)clampi((int)t.portaTime + enc, 0, 127); break;
            case 2:  t.cutoff   = (uint8_t)clampi((int)t.cutoff + enc, 0, 127); break;
            case 3:  t.reso     = (uint8_t)clampi((int)t.reso + enc, 0, 127); break;
            case 4:  t.attack   = (uint8_t)clampi((int)t.attack + enc, 0, 127); break;
            case 5:  t.decay    = (uint8_t)clampi((int)t.decay + enc, 0, 127); break;
            case 6:  t.release  = (uint8_t)clampi((int)t.release + enc, 0, 127); break;
            case 7:  t.vibRate  = (uint8_t)clampi((int)t.vibRate + enc, 0, 127); break;
            case 8:  t.vibDepth = (uint8_t)clampi((int)t.vibDepth + enc, 0, 127); break;
            case 9:  t.vibDelay = (uint8_t)clampi((int)t.vibDelay + enc, 0, 127); break;
            case 10: t.modWheel = (uint8_t)clampi((int)t.modWheel + enc, 0, 127); break;
        }
        break;
    case View::FX:
        switch (row) {
            case 0:  f.revType     = (uint8_t)clampi((int)f.revType + enc, 0, 7); break;
            case 1:  f.revLevel    = (uint8_t)clampi((int)f.revLevel + enc * 2, 0, 127); break;
            case 2:  f.revTime     = (uint8_t)clampi((int)f.revTime + enc * 2, 0, 127); break;
            case 3:  f.revFeedback = (uint8_t)clampi((int)f.revFeedback + enc * 2, 0, 127); break;
            case 4:  f.revPreLpf   = (uint8_t)clampi((int)f.revPreLpf + enc, 0, 7); break;
            case 5:  f.choType     = (uint8_t)clampi((int)f.choType + enc, 0, 7); break;
            case 6:  f.choLevel    = (uint8_t)clampi((int)f.choLevel + enc * 2, 0, 127); break;
            case 7:  f.choRate     = (uint8_t)clampi((int)f.choRate + enc, 0, 127); break;
            case 8:  f.choDepth    = (uint8_t)clampi((int)f.choDepth + enc, 0, 127); break;
            case 9:  f.choFeedback = (uint8_t)clampi((int)f.choFeedback + enc * 2, 0, 127); break;
            case 10: f.choDelay    = (uint8_t)clampi((int)f.choDelay + enc * 2, 0, 127); break;
            case 11: f.choToRev    = (uint8_t)clampi((int)f.choToRev + enc * 2, 0, 127); break;
            case 12: f.choPreLpf   = (uint8_t)clampi((int)f.choPreLpf + enc, 0, 7); break;
        }
        break;
    case View::MASTER:
        switch (row) {
            case 0:  f.masterVol = (uint8_t)clampi((int)f.masterVol + enc * 2, 0, 127); break;
            case 1:  f.keyShift  = (int8_t)clampi((int)f.keyShift + enc, -24, 24); break;
            case 2:  f.fineTune  = (int8_t)clampi((int)f.fineTune + enc, -100, 100); break;
            case 3:  f.masterPan = (uint8_t)clampi((int)f.masterPan + enc * 2, 1, 127); break;
            case 4: case 5: case 6: case 7:
                f.eqGain[row - 4] = (uint8_t)clampi((int)f.eqGain[row - 4] + enc, 0, 127);
                break;
            case 8: case 9: case 10: case 11:
                f.eqFreq[row - 8] = (uint8_t)clampi((int)f.eqFreq[row - 8] + enc, 0, 127);
                break;
        }
        break;
    case View::XPRT: {
        uint8_t* v[XPRT_ROWS] = {
            &_xCcCh, &_xCcNum, &_xCcVal, nullptr,
            &_xNrCh, &_xNrMsb, &_xNrLsb, &_xNrVal, nullptr,
            &_xGsA1, &_xGsA2, &_xGsA3, &_xGsVal, nullptr };
        if (v[row]) {
            int lo = (row == 0 || row == 4) ? 1 : 0;
            int hi = (row == 0 || row == 4) ? 16 : 127;
            *v[row] = (uint8_t)clampi((int)*v[row] + enc, lo, hi);
        }
        break;
    }
    case View::SONG:
        if (row >= SONG_FIXED && row < SONG_FIXED + CHAIN_LEN) {
            // chain entry: -- (0xFF) or P1..P8
            uint8_t i = (uint8_t)(row - SONG_FIXED);
            int cur = _song->chain[i] == 0xFF ? -1 : _song->chain[i];
            cur = clampi(cur + enc, -1, NUM_PATTERNS - 1);
            _song->chain[i] = (cur < 0) ? 0xFF : (uint8_t)cur;
            break;
        }
        switch (row) {
            case 0: {
                _editPat = (uint8_t)((_editPat + enc + NUM_PATTERNS) % NUM_PATTERNS);
                _eng->reqPattern = _editPat;
                if (_eng->running()) toast("queued at bar");
                break;
            }
            case 1: ep.length = (uint8_t)clampi((int)ep.length + enc, 1, NUM_STEPS); break;
            case 2: ep.swing  = (uint8_t)clampi((int)ep.swing + enc, 0, SWING_MAX); break;
            case 3: _song->spbIndex = (uint8_t)clampi((int)_song->spbIndex + enc, 0,
                                                      (int)NUM_SPB_OPTIONS - 1); break;
            case 4: _song->clockOut ^= 1; break;
            case 5: _song->scaleRoot = (uint8_t)((_song->scaleRoot + enc + 12) % 12); break;
            case 6: _song->scaleType = (uint8_t)((_song->scaleType + enc + SC_NUM) % SC_NUM); break;
            case 7: _song->chainLen  = (uint8_t)clampi((int)_song->chainLen + enc, 1, CHAIN_LEN); break;
            default:
                if (row == SONG_FIXED + CHAIN_LEN)          // Slot
                    _slot = (uint8_t)((_slot + enc + NUM_SONG_SLOTS) % NUM_SONG_SLOTS);
                else if (row == SONG_FIXED + CHAIN_LEN + 3) // Copy source
                    _copySrc = (uint8_t)((_copySrc + enc + NUM_PATTERNS) % NUM_PATTERNS);
                break;
        }
        break;
    default: break;
    }
}

void UI::listRowAction(uint8_t row) {
    switch (_view) {
    case View::MASTER:
        if (row == 12) { _eng->reqGmReset = 1; toast("GM reset"); }
        if (row == 13) { _eng->reqGsReset = 1; toast("GS reset"); }
        if (row == 14) { _eng->reqPanic = 1;   toast("panic"); }
        if (row == 15) { _eng->reqResendAll = 1; toast("resent all"); }
        break;
    case View::XPRT:
        if (row == 3)  { _eng->xA = _xCcCh; _eng->xB = _xCcNum; _eng->xC = _xCcVal;
                         _eng->xKind = 1; toast("CC sent"); }
        if (row == 8)  { _eng->xA = _xNrCh; _eng->xB = _xNrMsb; _eng->xC = _xNrLsb;
                         _eng->xD = _xNrVal; _eng->xKind = 2; toast("NRPN sent"); }
        if (row == 13) { _eng->xA = _xGsA1; _eng->xB = _xGsA2; _eng->xC = _xGsA3;
                         _eng->xD = _xGsVal; _eng->xKind = 3; toast("SysEx sent"); }
        break;
    case View::SONG: {
        uint8_t a = (uint8_t)(row - (SONG_FIXED + CHAIN_LEN + 1));
        if (a == 0) saveLoad(false);
        if (a == 1) saveLoad(true);
        if (a == 2) { _song->pattern[_editPat] = _song->pattern[_copySrc]; toast("copied"); }
        if (a == 3) { patternReset(_song->pattern[_editPat]); toast("pattern cleared"); }
        break;
    }
    default: break;
    }
}

void UI::listRowText(uint8_t row, char* buf, size_t n) {
    TrackCfg& t = _song->track[_track];
    FxParams& f = _song->fx;
    Pattern&  ep = _song->pattern[_editPat];
    char nm[18];

    switch (_view) {
    case View::INST:
        switch (row) {
            case 0: snprintf(buf, n, "Chan:  %u%s", t.channel,
                             t.channel == DRUM_CHANNEL ? " (drum)" : ""); break;
            case 1: snprintf(buf, n, "Mode:  %s", kModeNames[t.rhythm % RM_NUM]); break;
            case 2:
                if (trackIsDrum(t)) { gmKitName(t.program, nm, sizeof(nm));
                                      snprintf(buf, n, "Kit:   %s", nm); }
                else { gmInstrumentName(t.program, nm, sizeof(nm));
                       snprintf(buf, n, "Prog:  %s", nm); }
                break;
            case 3: snprintf(buf, n, "Bank:  %s", t.bankMSB ? "MT-32" : "GM"); break;
            case 4: snprintf(buf, n, "Oct:   %+d", t.octave); break;
            case 5: snprintf(buf, n, "Vol:   %u", t.vol); break;
            case 6: {
                int pv = (int)t.pan - 64;
                if (pv == 0)     snprintf(buf, n, "Pan:   C");
                else if (pv < 0) snprintf(buf, n, "Pan:   L%d", -pv);
                else             snprintf(buf, n, "Pan:   R%d", pv);
                break;
            }
            case 7:  snprintf(buf, n, "Rev:   %u", t.revSend); break;
            case 8:  snprintf(buf, n, "Cho:   %u", t.choSend); break;
            case 9:  snprintf(buf, n, "Bend:  %u semi", t.bendRange); break;
            case 10: snprintf(buf, n, "Out:   %s", kDestNames[t.dest % DEST_NUM]); break;
            case 11: snprintf(buf, n, "Mute:  %s", t.mute ? "YES" : "no"); break;
        }
        break;
    case View::SHAPE:
        switch (row) {
            case 0: snprintf(buf, n, "Len:   %u steps", soundingPat().tlen[_track]); break;
            case 1:
                if (t.portaTime == 0) snprintf(buf, n, "Porta: off");
                else                  snprintf(buf, n, "Porta: %u", t.portaTime);
                break;
            case 2:  snprintf(buf, n, "Cutoff:%u%s",  t.cutoff,  t.cutoff  == 64 ? "*" : ""); break;
            case 3:  snprintf(buf, n, "Reso:  %u%s",  t.reso,    t.reso    == 64 ? "*" : ""); break;
            case 4:  snprintf(buf, n, "Attack:%u%s",  t.attack,  t.attack  == 64 ? "*" : ""); break;
            case 5:  snprintf(buf, n, "Decay: %u%s",  t.decay,   t.decay   == 64 ? "*" : ""); break;
            case 6:  snprintf(buf, n, "Rel:   %u%s",  t.release, t.release == 64 ? "*" : ""); break;
            case 7:  snprintf(buf, n, "VibRat:%u%s",  t.vibRate, t.vibRate == 64 ? "*" : ""); break;
            case 8:  snprintf(buf, n, "VibDep:%u%s",  t.vibDepth,t.vibDepth== 64 ? "*" : ""); break;
            case 9:  snprintf(buf, n, "VibDly:%u%s",  t.vibDelay,t.vibDelay== 64 ? "*" : ""); break;
            case 10: snprintf(buf, n, "Mod:   %u",    t.modWheel); break;
        }
        break;
    case View::FX:
        switch (row) {
            case 0:  snprintf(buf, n, "RevTyp:%s", kRevTypeNames[f.revType & 7]); break;
            case 1:  snprintf(buf, n, "RevLvl:%u", f.revLevel); break;
            case 2:  snprintf(buf, n, "RevTim:%u", f.revTime); break;
            case 3:  snprintf(buf, n, "RevFbk:%u", f.revFeedback); break;
            case 4:  snprintf(buf, n, "RevLPF:%u", f.revPreLpf); break;
            case 5:  snprintf(buf, n, "ChoTyp:%s", kChoTypeNames[f.choType & 7]); break;
            case 6:  snprintf(buf, n, "ChoLvl:%u", f.choLevel); break;
            case 7:  snprintf(buf, n, "ChoRat:%u", f.choRate); break;
            case 8:  snprintf(buf, n, "ChoDep:%u", f.choDepth); break;
            case 9:  snprintf(buf, n, "ChoFbk:%u", f.choFeedback); break;
            case 10: snprintf(buf, n, "ChoDly:%u", f.choDelay); break;
            case 11: snprintf(buf, n, "Cho>Rv:%u", f.choToRev); break;
            case 12: snprintf(buf, n, "ChoLPF:%u", f.choPreLpf); break;
        }
        break;
    case View::MASTER: {
        static const char* const eqBand[4] = {"Lo", "MLo", "MHi", "Hi"};
        switch (row) {
            case 0: snprintf(buf, n, "MstVol:%u", f.masterVol); break;
            case 1: snprintf(buf, n, "KeySft:%+d semi", f.keyShift); break;
            case 2: snprintf(buf, n, "Tune:  %+d cent", f.fineTune); break;
            case 3: {
                int pv = (int)f.masterPan - 64;
                if (pv == 0)     snprintf(buf, n, "MstPan:C");
                else if (pv < 0) snprintf(buf, n, "MstPan:L%d", -pv);
                else             snprintf(buf, n, "MstPan:R%d", pv);
                break;
            }
            case 4: case 5: case 6: case 7:
                snprintf(buf, n, "EQ %sG: %+d", eqBand[row - 4], (int)f.eqGain[row - 4] - 64);
                break;
            case 8: case 9: case 10: case 11:
                snprintf(buf, n, "EQ %sF: %u", eqBand[row - 8], f.eqFreq[row - 8]);
                break;
            case 12: snprintf(buf, n, "[GM Reset]  REC"); break;
            case 13: snprintf(buf, n, "[GS Reset]  REC"); break;
            case 14: snprintf(buf, n, "[Panic]     REC"); break;
            case 15: snprintf(buf, n, "[Resend]    REC"); break;
        }
        break;
    }
    case View::XPRT:
        switch (row) {
            case 0:  snprintf(buf, n, "CC ch:  %u", _xCcCh); break;
            case 1:  snprintf(buf, n, "CC num: %u", _xCcNum); break;
            case 2:  snprintf(buf, n, "CC val: %u", _xCcVal); break;
            case 3:  snprintf(buf, n, "[Send CC]   REC"); break;
            case 4:  snprintf(buf, n, "NR ch:  %u", _xNrCh); break;
            case 5:  snprintf(buf, n, "NR msb: %02Xh", _xNrMsb); break;
            case 6:  snprintf(buf, n, "NR lsb: %02Xh", _xNrLsb); break;
            case 7:  snprintf(buf, n, "NR val: %u", _xNrVal); break;
            case 8:  snprintf(buf, n, "[Send NRPN] REC"); break;
            case 9:  snprintf(buf, n, "GS a1:  %02Xh", _xGsA1); break;
            case 10: snprintf(buf, n, "GS a2:  %02Xh", _xGsA2); break;
            case 11: snprintf(buf, n, "GS a3:  %02Xh", _xGsA3); break;
            case 12: snprintf(buf, n, "GS val: %u", _xGsVal); break;
            case 13: snprintf(buf, n, "[Send GS]   REC"); break;
        }
        break;
    case View::SONG:
        if (row >= SONG_FIXED && row < SONG_FIXED + CHAIN_LEN) {
            uint8_t i = (uint8_t)(row - SONG_FIXED);
            if (_song->chain[i] == 0xFF)
                snprintf(buf, n, " %02u:   --%s", i + 1, i < _song->chainLen ? "" : " (off)");
            else
                snprintf(buf, n, " %02u:   P%u%s", i + 1, _song->chain[i] + 1,
                         i < _song->chainLen ? "" : " (off)");
            break;
        }
        switch (row) {
            case 0: snprintf(buf, n, "Pattrn:P%u%s", _editPat + 1,
                             _editPat == _eng->playPat ? "" : " (q)"); break;
            case 1: snprintf(buf, n, "BarLen:%u", ep.length); break;
            case 2: snprintf(buf, n, "Swing: %u%%", ep.swing); break;
            case 3: snprintf(buf, n, "Resol: %u/beat",
                             kStepsPerBeatOptions[_song->spbIndex % NUM_SPB_OPTIONS]); break;
            case 4: snprintf(buf, n, "Clock: %s", _song->clockOut ? "out" : "off"); break;
            case 5: snprintf(buf, n, "Root:  %s", ROOT_NAMES[_song->scaleRoot % 12]); break;
            case 6: snprintf(buf, n, "Scale: %s", SCALE_NAMES[_song->scaleType % SC_NUM]); break;
            case 7: snprintf(buf, n, "ChnLen:%u", _song->chainLen); break;
            default: {
                uint8_t a = (uint8_t)(row - (SONG_FIXED + CHAIN_LEN));
                if (a == 0) snprintf(buf, n, "Slot:  %u%s", _slot + 1,
                                     Storage::exists(_slot) ? "*" : "");
                else if (a == 1) snprintf(buf, n, "[Save]      REC");
                else if (a == 2) snprintf(buf, n, "[Load]      REC");
                else if (a == 3) snprintf(buf, n, "[Copy P%u]   REC", _copySrc + 1);
                else             snprintf(buf, n, "[Clear pat] REC");
                break;
            }
        }
        break;
    default:
        snprintf(buf, n, "-");
        break;
    }
}

// ─── drawing ─────────────────────────────────────────────────────────────────
void UI::drawHeader() {
    char buf[20];
    oled.setFont(u8g2_font_5x7_tr);
    oled.drawStr(SCREEN_L, 7, _eng->running() ? ">" : "#");
    snprintf(buf, sizeof(buf), "%u", (unsigned)_song->bpm);
    oled.drawStr(SCREEN_L + 7, 7, buf);
    snprintf(buf, sizeof(buf), "P%u", (unsigned)_eng->playPat + 1);
    oled.drawStr(SCREEN_L + 24, 7, buf);
    oled.drawStr(SCREEN_L + 38, 7, kViewNames[(int)_view]);
    char lbl[12]; trackLabel(_track, lbl, sizeof(lbl));
    const TrackCfg& tc = _song->track[_track];
    snprintf(buf, sizeof(buf), "%02u%s%s", (unsigned)_track + 1,
             tc.mute ? "m" : (tc.solo ? "s" : " "), lbl);
    oled.drawStr(SCREEN_L + 62, 7, buf);
    oled.drawHLine(SCREEN_L, 9, SCREEN_W);
}

void UI::drawStepView() {
    Pattern& p = soundingPat();
    uint8_t tl = (uint8_t)clampi(p.tlen[_track], 1, NUM_STEPS);
    uint8_t playhead = _eng->trackStep[_track];
    uint8_t win  = (uint8_t)(_cursor / STEPS_PER_PAGE);
    uint8_t base = (uint8_t)(win * STEPS_PER_PAGE);
    const int y0 = 14, ch = 22, gap = 1;
    const int cw = SCREEN_W / 8;                        // 8 columns per row
    const int x0 = SCREEN_L + (SCREEN_W - cw * 8) / 2;   // center the leftover

    for (int i = 0; i < STEPS_PER_PAGE; i++) {
        uint8_t sidx = (uint8_t)(base + i);
        int col = i % 8, row = i / 8;
        int x = x0 + col * cw;
        int y = y0 + row * (ch + gap);
        const Step& st = p.step[_track][sidx];
        bool on = stepOn(st) && sidx < tl;
        if (on) oled.drawBox(x, y, cw - 2, ch - 4);
        else    oled.drawFrame(x, y, cw - 2, ch - 4);
        if (sidx >= tl) oled.drawHLine(x, y + ch - 3, cw - 2);   // beyond length
        if (stepAccent(st)) oled.drawDisc(x + cw - 5, y + 2, 1);
        if (stepTie(st))    oled.drawHLine(x, y + (ch - 4) / 2, cw - 2);
        if (st.ratchet > 1)                                       // ratchet ticks
            for (int k = 0; k < st.ratchet; k++)
                oled.drawVLine(x + 2 + k * 3, y + ch - 8, 3);
        if (on && st.prob < 100) oled.drawPixel(x + 1, y + 1);    // prob marker
        if (sidx == _cursor) oled.drawFrame(x - 1, y - 1, cw, ch - 2);
        if (_eng->running() && sidx == playhead)
            oled.drawBox(x + 2, y + ch - 4, cw - 6, 2);
    }

    // cursor step detail line
    char buf[30], nn[16];
    const Step& cs = p.step[_track][_cursor];
    char pg[8] = "";
    if (tl > STEPS_PER_PAGE)
        snprintf(pg, sizeof(pg), " %u/%u", win + 1, (tl + 15) / 16);
    char val[12];
    switch (_stepField) {
        case SF_VEL:   snprintf(val, sizeof(val), "%u", cs.vel); break;
        case SF_GATE:  snprintf(val, sizeof(val), "%u%%", cs.gate); break;
        case SF_PROB:  snprintf(val, sizeof(val), "%u%%", cs.prob); break;
        case SF_MICRO: snprintf(val, sizeof(val), "%+d", cs.micro); break;
        case SF_RATCH: snprintf(val, sizeof(val), "x%u", cs.ratchet); break;
        default: {
            if (trackIsDrum(_song->track[_track])) gmDrumName(cs.note, val, sizeof(val));
            else {
                uint8_t played = scaleSnap(_song->scaleRoot, _song->scaleType, cs.note);
                noteName(played, nn, sizeof(nn));
                snprintf(val, sizeof(val), "%s%s", nn, played != cs.note ? "*" : "");
            }
            break;
        }
    }
    snprintf(buf, sizeof(buf), "S%02u %s:%s%s%s%s", _cursor + 1,
             kStepFieldNames[_stepField], val,
             stepAccent(cs) ? " A" : "", stepTie(cs) ? " ~" : "", pg);
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(SCREEN_L, 63, buf);
}

void UI::drawMixView() {
    const int bw = SCREEN_W / 16;                       // 16 track columns
    const int x0 = SCREEN_L + (SCREEN_W - bw * 16) / 2;  // center the leftover
    const int yBase = 56, hMax = 36;
    for (int t = 0; t < NUM_TRACKS; t++) {
        const TrackCfg& c = _song->track[t];
        int x = x0 + t * bw;
        int h = 1 + ((int)c.vol * hMax) / 127;
        if (c.mute) oled.drawFrame(x + 1, yBase - h, bw - 3, h);   // hollow = muted
        else        oled.drawBox(x + 1, yBase - h, bw - 3, h);
        if (c.solo) oled.drawDisc(x + bw / 2, yBase + 3, 1);
        if (t == _track) oled.drawFrame(x, yBase - hMax - 2, bw - 1, hMax + 4);
    }
    char lbl[14], buf[28];
    trackLabel(_track, lbl, sizeof(lbl));
    const TrackCfg& c = _song->track[_track];
    snprintf(buf, sizeof(buf), "T%02u %s v%u%s%s", _track + 1, lbl, c.vol,
             c.mute ? " MUTE" : "", c.solo ? " SOLO" : "");
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(SCREEN_L, 63, buf);
}

void UI::drawListView() {
    uint8_t count = listRowCount();
    oled.setFont(u8g2_font_5x7_tr);
    char row[26];
    const int rowW = SCREEN_W - 8;       // reserve the right edge for the scrollbar
    for (uint8_t i = 0; i < 7; i++) {
        uint8_t r = (uint8_t)(_scroll + i);
        if (r >= count) break;
        int y = 16 + i * 7;
        listRowText(r, row, sizeof(row));
        if (r == _field) { oled.drawBox(SCREEN_L, y - 6, rowW, 8); oled.setDrawColor(0); }
        oled.drawStr(SCREEN_L + 3, y, row);
        oled.setDrawColor(1);
    }
    // scrollbar
    if (count > 7) {
        int barH = (7 * 52) / count; if (barH < 4) barH = 4;
        int barY = 11 + ((int)_scroll * (52 - barH)) / (count - 7);
        oled.drawVLine(SCREEN_R - 3, 11, 52);
        oled.drawBox(SCREEN_R - 5, barY, 3, barH);
    }
}

void UI::render() {
    uint32_t now = millis();
    if (now - _lastDraw < 40) return;     // ~25 fps
    _lastDraw = now;

    oled.clearBuffer();
    drawHeader();
    switch (_view) {
        case View::STEP: drawStepView(); break;
        case View::MIX:  drawMixView();  break;
        default:         drawListView(); break;
    }
    if (_toast[0] && now < _toastUntil) {
        oled.setFont(u8g2_font_6x10_tr);
        int tw = oled.getStrWidth(_toast);
        int w  = tw + 6;
        int bx = SCREEN_L + (SCREEN_W - w) / 2;
        if (bx < SCREEN_L) bx = SCREEN_L;
        oled.setDrawColor(0); oled.drawBox(bx, 26, w, 13); oled.setDrawColor(1);
        oled.drawFrame(bx, 26, w, 13);
        oled.drawStr(bx + 3, 36, _toast);
    } else if (_toast[0] && now >= _toastUntil) _toast[0] = 0;

    oled.sendBuffer();
}
