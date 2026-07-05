// ============================================================================
//  Sequencer.cpp  -  Pattern model + engine implementation
// ============================================================================
#include "Sequencer.h"
#include "GMSynth.h"
#include "Scales.h"

// small clamp helpers
static inline int clampi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }

// ===========================================================================
//  Song initialisation
// ===========================================================================
void Sequencer::initDefaultSong() {
  data.bpm       = BPM_DEFAULT;
  data.swing     = 0;
  data.spbIndex  = 3;                 // 4 steps/beat = 16th notes
  data.clockSrc  = CLK_INTERNAL;
  data.scaleRoot = 0;                 // C
  data.scaleType = 1;                 // Major
  data.scaleLock = LOCK_OFF;
  data.revType   = 4;                 // SAM2695 default reverb program
  data.revLevel  = 64;
  data.revTime   = 64;
  data.choType   = 2;                 // SAM2695 default chorus program
  data.choLevel  = 64;
  data.choRate   = 64;
  data.choDepth  = 64;
  data.masterVol = 120;

  for (uint8_t t = 0; t < MAX_TRACKS; t++) {
    Track& tr = data.tracks[t];
    tr.channel    = t + 1;            // track index 9 -> channel 10 (drums)
    tr.program    = 0;
    tr.bankMSB    = 0;
    tr.octave     = 0;
    tr.transpose  = 0;
    tr.length     = DEFAULT_STEPS;
    tr.direction  = DIR_FWD;
    tr.clkDiv     = 0;                // /1
    tr.humVel     = 0;
    tr.humTime    = 0;
    tr.vol        = 100;
    tr.pan        = 64;
    tr.revSend    = 40;
    tr.choSend    = 0;
    tr.expression = 127;
    tr.modulation = 0;
    tr.cutoff     = 64;
    tr.resonance  = 64;
    tr.attack     = 64;
    tr.decay      = 64;
    tr.release    = 64;
    tr.vibRate    = 64;
    tr.vibDepth   = 64;
    tr.vibDelay   = 64;
    tr.bendRange  = 2;
    tr.portaOn    = 0;
    tr.portaTime  = 20;
    tr.sustain    = 0;
    tr.mute       = 0;
    tr.solo       = 0;
    tr.stepField  = SF_NOTE;
    tr.curStep    = 0;
    bool drum     = (tr.channel == DRUM_CHANNEL);
    for (uint8_t s = 0; s < MAX_STEPS; s++) {
      Step& sp = tr.steps[s];
      sp.active  = 0;
      sp.note    = drum ? 36 : 60;    // bass drum / middle C
      sp.vel     = 100;
      sp.gate    = 80;
      sp.prob    = 100;
      sp.ratchet = 1;
      sp.micro   = 0;
      sp.tie     = 0;
    }
  }
}

// ===========================================================================
//  Solo / mute / scale helpers
// ===========================================================================
bool Sequencer::anySolo() const {
  for (uint8_t t = 0; t < MAX_TRACKS; t++) if (data.tracks[t].solo) return true;
  return false;
}
bool Sequencer::trackAudible(uint8_t t) const {
  if (anySolo()) return data.tracks[t].solo != 0;
  return data.tracks[t].mute == 0;
}
uint16_t Sequencer::scaleMask() const {
  return kScales[data.scaleType % NUM_SCALES].mask;
}

// ===========================================================================
//  core0 edit helpers (clamped, atomic byte writes)
// ===========================================================================
void Sequencer::audition(uint8_t t, uint8_t note) {
  audCh   = data.tracks[t].channel;
  audNote = note;
  audVel  = 110;
  audReq  = 1;
}

void Sequencer::toggleStep(uint8_t t, uint8_t s) {
  data.tracks[t].steps[s].active ^= 1;
}

void Sequencer::editStepField(uint8_t t, uint8_t s, uint8_t field, int delta) {
  Step& sp = data.tracks[t].steps[s];
  switch (field) {
    case SF_NOTE:
      // With the scale lock engaged the encoder walks scale degrees instead
      // of raw semitones (drums stay chromatic - the kit map is fixed).
      if (data.scaleLock != LOCK_OFF && !isDrumTrack(t)) {
        int dir = delta >= 0 ? 1 : -1;
        for (int i = 0; i < abs(delta); i++)
          sp.note = scaleStep(sp.note, dir, data.scaleRoot, scaleMask());
      } else {
        sp.note = (uint8_t)clampi(sp.note + delta, 0, 127);
      }
      break;
    case SF_VEL:     sp.vel     = (uint8_t)clampi(sp.vel   + delta, 1, 127); break;
    case SF_GATE:    sp.gate    = (uint8_t)clampi(sp.gate  + delta * 5, 1, 100); break;
    case SF_PROB:    sp.prob    = (uint8_t)clampi(sp.prob  + delta * 5, 0, 100); break;
    case SF_RATCHET: sp.ratchet = (uint8_t)clampi(sp.ratchet + delta, 1, MAX_RATCHET); break;
    case SF_MICRO:   sp.micro   = (int8_t) clampi(sp.micro + delta, -12, 12); break;
    case SF_TIE:     if (delta) sp.tie ^= 1; break;
    default: break;
  }
}

void Sequencer::setProgram(uint8_t t, int d){ data.tracks[t].program = (uint8_t)clampi(data.tracks[t].program + d, 0, 127); }
void Sequencer::setBank(uint8_t t){ data.tracks[t].bankMSB = data.tracks[t].bankMSB ? 0 : 127; }
void Sequencer::setChannel(uint8_t t, int d){ data.tracks[t].channel = (uint8_t)clampi(data.tracks[t].channel + d, 1, 16); }
void Sequencer::setOctave(uint8_t t, int d){ data.tracks[t].octave = (int8_t)clampi(data.tracks[t].octave + d, -3, 3); }
void Sequencer::setTranspose(uint8_t t, int d){ data.tracks[t].transpose = (int8_t)clampi(data.tracks[t].transpose + d, -12, 12); }
void Sequencer::setLength(uint8_t t, int d){ data.tracks[t].length = (uint8_t)clampi(data.tracks[t].length + d, MIN_STEPS, MAX_STEPS); }
void Sequencer::setDirection(uint8_t t, int d){ data.tracks[t].direction = (uint8_t)clampi(data.tracks[t].direction + d, 0, DIR_COUNT - 1); }
void Sequencer::setClkDiv(uint8_t t, int d){ data.tracks[t].clkDiv = (uint8_t)clampi(data.tracks[t].clkDiv + d, 0, (int)NUM_DIV_OPTIONS - 1); }
void Sequencer::setHumVel(uint8_t t, int d){ data.tracks[t].humVel = (uint8_t)clampi(data.tracks[t].humVel + d, 0, 32); }
void Sequencer::setHumTime(uint8_t t, int d){ data.tracks[t].humTime = (uint8_t)clampi(data.tracks[t].humTime + d, 0, 12); }
void Sequencer::setVol(uint8_t t, int d){ data.tracks[t].vol = (uint8_t)clampi(data.tracks[t].vol + d * 4, 0, 127); }
void Sequencer::setPan(uint8_t t, int d){ data.tracks[t].pan = (uint8_t)clampi(data.tracks[t].pan + d * 4, 0, 127); }
void Sequencer::setRevSend(uint8_t t, int d){ data.tracks[t].revSend = (uint8_t)clampi(data.tracks[t].revSend + d * 4, 0, 127); }
void Sequencer::setChoSend(uint8_t t, int d){ data.tracks[t].choSend = (uint8_t)clampi(data.tracks[t].choSend + d * 4, 0, 127); }
void Sequencer::setExpression(uint8_t t, int d){ data.tracks[t].expression = (uint8_t)clampi(data.tracks[t].expression + d * 4, 0, 127); }
void Sequencer::setModulation(uint8_t t, int d){ data.tracks[t].modulation = (uint8_t)clampi(data.tracks[t].modulation + d * 4, 0, 127); }
void Sequencer::setCutoff(uint8_t t, int d){ data.tracks[t].cutoff = (uint8_t)clampi(data.tracks[t].cutoff + d * 2, 0, 127); }
void Sequencer::setResonance(uint8_t t, int d){ data.tracks[t].resonance = (uint8_t)clampi(data.tracks[t].resonance + d * 2, 0, 127); }
void Sequencer::setAttack(uint8_t t, int d){ data.tracks[t].attack = (uint8_t)clampi(data.tracks[t].attack + d * 2, 0, 127); }
void Sequencer::setDecay(uint8_t t, int d){ data.tracks[t].decay = (uint8_t)clampi(data.tracks[t].decay + d * 2, 0, 127); }
void Sequencer::setRelease(uint8_t t, int d){ data.tracks[t].release = (uint8_t)clampi(data.tracks[t].release + d * 2, 0, 127); }
void Sequencer::setVibRate(uint8_t t, int d){ data.tracks[t].vibRate = (uint8_t)clampi(data.tracks[t].vibRate + d * 2, 0, 127); }
void Sequencer::setVibDepth(uint8_t t, int d){ data.tracks[t].vibDepth = (uint8_t)clampi(data.tracks[t].vibDepth + d * 2, 0, 127); }
void Sequencer::setVibDelay(uint8_t t, int d){ data.tracks[t].vibDelay = (uint8_t)clampi(data.tracks[t].vibDelay + d * 2, 0, 127); }
void Sequencer::setBendRange(uint8_t t, int d){ data.tracks[t].bendRange = (uint8_t)clampi(data.tracks[t].bendRange + d, 0, 24); }
void Sequencer::togglePorta(uint8_t t){ data.tracks[t].portaOn ^= 1; }
void Sequencer::setPortaTime(uint8_t t, int d){ data.tracks[t].portaTime = (uint8_t)clampi(data.tracks[t].portaTime + d * 2, 0, 127); }
void Sequencer::toggleSustain(uint8_t t){ data.tracks[t].sustain ^= 1; }
void Sequencer::toggleMute(uint8_t t){ data.tracks[t].mute ^= 1; }
void Sequencer::toggleSolo(uint8_t t){ data.tracks[t].solo ^= 1; }

void Sequencer::setBpm(int d){ data.bpm = (uint16_t)clampi((int)data.bpm + d, BPM_MIN, BPM_MAX); }
void Sequencer::setSwing(int d){ data.swing = (uint8_t)clampi(data.swing + d, 0, 75); }
void Sequencer::setSpb(int d){ data.spbIndex = (uint8_t)clampi(data.spbIndex + d, 0, (int)NUM_SPB_OPTIONS - 1); }
void Sequencer::setScaleRoot(int d){ data.scaleRoot = (uint8_t)((data.scaleRoot + 12 + (d % 12)) % 12); }
void Sequencer::setScaleType(int d){ data.scaleType = (uint8_t)clampi(data.scaleType + d, 0, NUM_SCALES - 1); }
void Sequencer::setScaleLock(int d){ if (d) data.scaleLock = data.scaleLock == LOCK_OFF ? LOCK_ON : LOCK_OFF; }
void Sequencer::setRevType(int d){ data.revType = (uint8_t)clampi(data.revType + d, 0, 7); }
void Sequencer::setRevLevel(int d){ data.revLevel = (uint8_t)clampi(data.revLevel + d * 2, 0, 127); }
void Sequencer::setRevTime(int d){ data.revTime = (uint8_t)clampi(data.revTime + d * 2, 0, 127); }
void Sequencer::setChoType(int d){ data.choType = (uint8_t)clampi(data.choType + d, 0, 7); }
void Sequencer::setChoLevel(int d){ data.choLevel = (uint8_t)clampi(data.choLevel + d * 2, 0, 127); }
void Sequencer::setChoRate(int d){ data.choRate = (uint8_t)clampi(data.choRate + d * 2, 0, 127); }
void Sequencer::setChoDepth(int d){ data.choDepth = (uint8_t)clampi(data.choDepth + d * 2, 0, 127); }
void Sequencer::setMasterVol(int d){ data.masterVol = (uint8_t)clampi(data.masterVol + d * 4, 0, 127); }

void Sequencer::quantizeTrack(uint8_t t) {
  if (isDrumTrack(t)) return;
  uint16_t mask = scaleMask();
  for (uint8_t s = 0; s < MAX_STEPS; s++)
    data.tracks[t].steps[s].note = scaleQuantize(data.tracks[t].steps[s].note, data.scaleRoot, mask);
}
void Sequencer::quantizeAll() {
  for (uint8_t t = 0; t < MAX_TRACKS; t++) quantizeTrack(t);
}

void Sequencer::clearTrack(uint8_t t){
  for (uint8_t s = 0; s < MAX_STEPS; s++) data.tracks[t].steps[s].active = 0;
}

// ===========================================================================
//  ENGINE (core1)
// ===========================================================================
void Sequencer::engineBegin() {
  for (int i = 0; i < MAX_EVENTS; i++) events[i].used = 0;
  resetPlayheads();
  shadowValid = false;
  reqResendAll = 1;        // push all settings once after GM reset
  refreshPeriod();
}

void Sequencer::refreshPeriod() {
  uint16_t bpm = data.bpm; if (bpm < BPM_MIN) bpm = BPM_MIN; if (bpm > BPM_MAX) bpm = BPM_MAX;
  uint8_t  spb = stepsPerBeat();
  periodDen   = (uint32_t)bpm * PPQN;
  periodWhole = 60000000UL / periodDen;
  periodFrac  = 60000000UL % periodDen;
  lastBpm = bpm; lastSpb = spb;
}

void Sequencer::reconcile() {
  bool full = !shadowValid || reqResendAll;
  if (reqResendAll) reqResendAll = 0;

  for (uint8_t t = 0; t < MAX_TRACKS; t++) {
    Track& tr = data.tracks[t];
    TrShadow& sh = shadow[t];
    bool chChanged = full || sh.channel != tr.channel;
    uint8_t ch = tr.channel;

    if (chChanged || sh.bank != tr.bankMSB) {
      GMSynth::bankSelect(ch, tr.bankMSB);
      GMSynth::programChange(ch, tr.program);     // program must follow a bank change
      sh.bank = tr.bankMSB; sh.program = tr.program;
    } else if (sh.program != tr.program) {
      GMSynth::programChange(ch, tr.program);
      sh.program = tr.program;
    }
    if (chChanged || sh.vol  != tr.vol)        { GMSynth::setVolume(ch, tr.vol);            sh.vol  = tr.vol; }
    if (chChanged || sh.pan  != tr.pan)        { GMSynth::setPan(ch, tr.pan);               sh.pan  = tr.pan; }
    if (chChanged || sh.rev  != tr.revSend)    { GMSynth::setReverbSend(ch, tr.revSend);    sh.rev  = tr.revSend; }
    if (chChanged || sh.cho  != tr.choSend)    { GMSynth::setChorusSend(ch, tr.choSend);    sh.cho  = tr.choSend; }
    if (chChanged || sh.expr != tr.expression) { GMSynth::setExpression(ch, tr.expression); sh.expr = tr.expression; }
    if (chChanged || sh.mod  != tr.modulation) { GMSynth::setModulation(ch, tr.modulation); sh.mod  = tr.modulation; }
    if (chChanged || sh.cutoff != tr.cutoff)   { GMSynth::setCutoff(ch, tr.cutoff);         sh.cutoff = tr.cutoff; }
    if (chChanged || sh.reso != tr.resonance)  { GMSynth::setResonance(ch, tr.resonance);   sh.reso = tr.resonance; }
    if (chChanged || sh.atk  != tr.attack)     { GMSynth::setAttack(ch, tr.attack);         sh.atk  = tr.attack; }
    if (chChanged || sh.dcy  != tr.decay)      { GMSynth::setDecay(ch, tr.decay);           sh.dcy  = tr.decay; }
    if (chChanged || sh.rel  != tr.release)    { GMSynth::setRelease(ch, tr.release);       sh.rel  = tr.release; }
    if (chChanged || sh.vibR != tr.vibRate)    { GMSynth::setVibratoRate(ch, tr.vibRate);   sh.vibR = tr.vibRate; }
    if (chChanged || sh.vibD != tr.vibDepth)   { GMSynth::setVibratoDepth(ch, tr.vibDepth); sh.vibD = tr.vibDepth; }
    if (chChanged || sh.vibDl!= tr.vibDelay)   { GMSynth::setVibratoDelay(ch, tr.vibDelay); sh.vibDl= tr.vibDelay; }
    if (chChanged || sh.bend != tr.bendRange)  { GMSynth::setBendRange(ch, tr.bendRange);   sh.bend = tr.bendRange; }
    if (chChanged || sh.portaOn != tr.portaOn) { GMSynth::setPortamento(ch, tr.portaOn);    sh.portaOn = tr.portaOn; }
    if (chChanged || sh.portaT != tr.portaTime){ GMSynth::setPortaTime(ch, tr.portaTime);   sh.portaT = tr.portaTime; }
    if (chChanged || sh.sus  != tr.sustain)    { GMSynth::setSustain(ch, tr.sustain);       sh.sus  = tr.sustain; }
    sh.channel = tr.channel;
  }

  if (full || shRevType != data.revType) {
    for (uint8_t ch = 1; ch <= 16; ch++) GMSynth::setReverbType(ch, data.revType);
    shRevType = data.revType;
  }
  if (full || shChoType != data.choType) {
    for (uint8_t ch = 1; ch <= 16; ch++) GMSynth::setChorusType(ch, data.choType);
    shChoType = data.choType;
  }
  if (full || shRevLevel != data.revLevel) { GMSynth::setMasterReverbLevel(data.revLevel); shRevLevel = data.revLevel; }
  if (full || shRevTime  != data.revTime)  { GMSynth::setMasterReverbTime(data.revTime);   shRevTime  = data.revTime; }
  if (full || shChoLevel != data.choLevel) { GMSynth::setMasterChorusLevel(data.choLevel); shChoLevel = data.choLevel; }
  if (full || shChoRate  != data.choRate)  { GMSynth::setMasterChorusRate(data.choRate);   shChoRate  = data.choRate; }
  if (full || shChoDepth != data.choDepth) { GMSynth::setMasterChorusDepth(data.choDepth); shChoDepth = data.choDepth; }
  if (full || shMasterVol != data.masterVol) {
    GMSynth::masterVolume(data.masterVol);
    shMasterVol = data.masterVol;
  }
  shadowValid = true;
}

void Sequencer::resetPlayheads() {
  for (uint8_t t = 0; t < MAX_TRACKS; t++) {
    ePos[t] = -1;         // first advance lands on step 0 (FWD) / len-1 (REV)
    ePp[t]  = 1;
    eAdv[t] = 0;
  }
}

void Sequencer::startFromTop() {
  flushAllOffs();
  for (int i = 0; i < MAX_EVENTS; i++) events[i].used = 0;
  gTick = 0; accFrac = 0;
  resetPlayheads();
  refreshPeriod();
  eRunning = true; isRunning = 1;
  nextUs = time_us_64();
  GMSynth::start();          // 0xFA
}

void Sequencer::startContinue() {
  refreshPeriod();
  eRunning = true; isRunning = 1;
  nextUs = time_us_64();
  GMSynth::cont();           // 0xFB
}

void Sequencer::stopTransport() {
  eRunning = false; isRunning = 0;
  GMSynth::stop();           // 0xFC
  flushAllOffs();
}

void Sequencer::flushAllOffs() {
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (events[i].used && events[i].type == EV_ON) events[i].used = 0; // cancel pending ons
  }
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (events[i].used && events[i].type == EV_OFF) {
      GMSynth::noteOff(events[i].ch, events[i].note);
      events[i].used = 0;
    }
  }
  GMSynth::panic();          // belt-and-braces: silence everything
}

void Sequencer::scheduleEvent(uint32_t tick, uint8_t type, uint8_t ch, uint8_t note, uint8_t vel) {
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (!events[i].used) {
      events[i] = { tick, type, ch, note, vel, 1 };
      return;
    }
  }
  // queue full: drop silently (extremely unlikely with MAX_EVENTS=256)
}

void Sequencer::advanceTrack(uint8_t t) {
  Track& tr = data.tracks[t];
  int len = tr.length; if (len < 1) len = 1;
  int pos = ePos[t];
  switch (tr.direction) {
    default:
    case DIR_FWD:
      pos = (pos + 1) % len;
      break;
    case DIR_REV:
      pos = (pos <= 0) ? len - 1 : pos - 1;
      break;
    case DIR_PINGPONG:
      pos += ePp[t];
      if (pos >= len) { pos = (len >= 2) ? len - 2 : 0; ePp[t] = -1; }
      if (pos < 0)    { pos = (len >= 2) ? 1 : 0;       ePp[t] = 1;  }
      break;
    case DIR_RANDOM:
      pos = (int)(rng() % (uint32_t)len);
      break;
  }
  if (pos >= len) pos = len - 1;
  if (pos < 0) pos = 0;
  ePos[t] = (int16_t)pos;
  eAdv[t]++;
}

void Sequencer::triggerStep(uint32_t stepCount) {
  const uint16_t tps = ticksPerStep();

  for (uint8_t t = 0; t < MAX_TRACKS; t++) {
    Track& tr = data.tracks[t];
    const uint8_t div = kClockDivOptions[tr.clkDiv % NUM_DIV_OPTIONS];
    if (stepCount % div) continue;               // this track sits this grid step out

    advanceTrack(t);
    const uint8_t si = (uint8_t)ePos[t];
    tr.curStep = si;                             // playhead for the UI

    if (!trackAudible(t)) continue;
    Step& sp = tr.steps[si];
    if (!sp.active) continue;
    if (sp.prob < 100 && (int)(rng() % 100) >= sp.prob) continue;

    const uint16_t trackTps = tps * div;         // ticks this track holds one step
    const int swingTicks = ((int)data.swing * trackTps) / 100;
    const bool offBeat = ((eAdv[t] & 1u) == 0);  // eAdv was ++'d: even = 2nd/4th/... step

    int offset = (offBeat ? swingTicks : 0) + sp.micro;
    if (tr.humTime) offset += (int)(rng() % (tr.humTime + 1));
    if (offset < 0) offset = 0;
    if (offset >= (int)trackTps) offset = trackTps - 1;

    uint8_t ch = tr.channel;
    uint8_t note;
    if (ch == DRUM_CHANNEL) {
      note = sp.note;                            // drum map is fixed, no transpose
    } else {
      int n = (int)sp.note + (int)tr.octave * 12 + (int)tr.transpose;
      n = clampi(n, 0, 127);
      if (data.scaleLock != LOCK_OFF)
        n = scaleQuantize((uint8_t)n, data.scaleRoot, scaleMask());
      note = (uint8_t)n;
    }

    int vel = sp.vel;
    if (tr.humVel) vel += (int)(rng() % (2u * tr.humVel + 1)) - (int)tr.humVel;
    vel = clampi(vel, 1, 127);

    int gateTicks = sp.tie ? trackTps : ((int)sp.gate * trackTps) / 100;
    if (gateTicks < 1) gateTicks = 1;

    const uint8_t nHits = sp.ratchet < 1 ? 1 : (sp.ratchet > MAX_RATCHET ? MAX_RATCHET : sp.ratchet);
    const uint32_t baseTick = gTick + (uint32_t)offset;

    if (nHits == 1) {
      scheduleEvent(baseTick, EV_ON, ch, note, (uint8_t)vel);
      scheduleEvent(baseTick + (uint32_t)gateTicks, EV_OFF, ch, note, 0);
    } else {
      // Ratchet: subdivide the step into nHits equal retriggers.
      const uint32_t interval = trackTps / nHits;
      for (uint8_t i = 0; i < nHits; i++) {
        uint32_t on  = baseTick + i * interval;
        uint32_t g   = (uint32_t)gateTicks;
        uint32_t cap = interval > 1 ? interval - 1 : 1;
        if (!(sp.tie && i == nHits - 1) && g > cap) g = cap;
        scheduleEvent(on, EV_ON, ch, note, (uint8_t)vel);
        scheduleEvent(on + g, EV_OFF, ch, note, 0);
      }
    }
  }
}

void Sequencer::serviceEvents(uint32_t curTick) {
  // OFFs first so a same-tick retrigger isn't killed by a stale note-off
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (events[i].used && events[i].type == EV_OFF && events[i].tick <= curTick) {
      GMSynth::noteOff(events[i].ch, events[i].note);
      events[i].used = 0;
    }
  }
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (events[i].used && events[i].type == EV_ON && events[i].tick <= curTick) {
      GMSynth::noteOn(events[i].ch, events[i].note, events[i].vel);
      events[i].used = 0;
    }
  }
}

void Sequencer::tick() {
  // recompute timing if tempo / resolution changed
  if (data.bpm != lastBpm || stepsPerBeat() != lastSpb) refreshPeriod();

  // schedule the next tick time (drift-free integer accumulator)
  uint32_t inc = periodWhole;
  accFrac += periodFrac;
  if (accFrac >= periodDen) { accFrac -= periodDen; inc += 1; }
  nextUs += inc;

  // MIDI clock out on the 24-PPQN grid (downbeat included)
  if ((gTick % MIDI_CLOCK_DIV) == 0) GMSynth::clockTick();

  // step boundary -> queue this step's hits
  const uint16_t tps = ticksPerStep();
  if ((gTick % tps) == 0) triggerStep(gTick / tps);

  // fire everything due at this tick
  serviceEvents(gTick);

  gTick++;
}

void Sequencer::engineService() {
  // ---- one-shot requests ----
  if (reqPanic)   { reqPanic = 0; flushAllOffs(); }
  if (reqGmReset) { reqGmReset = 0; GMSynth::gmReset(); delay(5); reqResendAll = 1; }

  // ---- push setting changes to the synth ----
  reconcile();

  // ---- transport requests ----
  if (reqStart)    { reqStart = 0;    startFromTop(); }
  if (reqContinue) { reqContinue = 0; startContinue(); }
  if (reqStop)     { reqStop = 0;     stopTransport(); }

  // ---- audition (wall-clock, works whether or not the transport runs) ----
  if (audReq) {
    audReq = 0;
    if (audActive) GMSynth::noteOff(audActiveCh, audActiveNote);
    audActiveCh = audCh; audActiveNote = audNote;
    GMSynth::noteOn(audActiveCh, audActiveNote, audVel);
    audActive = true;
    audOffUs = time_us_64() + 300000ULL;     // 300 ms preview
  }
  if (audActive && time_us_64() >= audOffUs) {
    GMSynth::noteOff(audActiveCh, audActiveNote);
    audActive = false;
  }

  // ---- timing ----
  if (eRunning) {
    uint64_t now = time_us_64();
    if (now >= nextUs) {
      // if we fell behind (e.g. after a long edit), catch up without busy-spinning
      if (now - nextUs > 50000ULL) nextUs = now;   // resync guard (>50ms gap)
      tick();
    } else {
      // short, precise spin to the next tick edge keeps jitter in the low-µs range
      uint64_t remaining = nextUs - now;
      if (remaining > 200) sleep_us((uint32_t)(remaining - 100));
      else { while (time_us_64() < nextUs) tight_loop_contents(); tick(); }
    }
  } else {
    sleep_us(200);
  }
}
