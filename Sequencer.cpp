// ============================================================================
//  Sequencer.cpp  -  Pattern model + engine implementation
// ============================================================================
#include "Sequencer.h"
#include "GMSynth.h"

// small clamp helpers
static inline int clampi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }

// ===========================================================================
//  Song initialisation
// ===========================================================================
void Sequencer::initDefaultSong() {
  data.bpm       = BPM_DEFAULT;
  data.swing     = 0;
  data.spbIndex  = 3;                 // 4 steps/beat = 16th notes
  data.revType   = 4;                 // SAM2695 default reverb program
  data.choType   = 2;                 // SAM2695 default chorus program
  data.masterVol = 120;
  data.clockSrc  = CLK_INTERNAL;

  for (uint8_t t = 0; t < MAX_TRACKS; t++) {
    Track& tr = data.tracks[t];
    tr.channel   = t + 1;             // track index 9 -> channel 10 (drums)
    tr.program   = 0;
    tr.bankMSB   = 0;
    tr.octave    = 0;
    tr.length    = DEFAULT_STEPS;
    tr.vol       = 100;
    tr.pan       = 64;
    tr.revSend   = 40;
    tr.choSend   = 0;
    tr.mute      = 0;
    tr.solo      = 0;
    tr.stepField = SF_NOTE;
    tr.curStep   = 0;
    bool drum    = (tr.channel == DRUM_CHANNEL);
    for (uint8_t s = 0; s < MAX_STEPS; s++) {
      Step& sp = tr.steps[s];
      sp.active = 0;
      sp.note   = drum ? 36 : 60;     // bass drum / middle C
      sp.vel    = 100;
      sp.gate   = 80;
      sp.prob   = 100;
      sp.micro  = 0;
      sp._rsvd  = 0;
      sp._pad   = 0;
    }
  }
}

// ===========================================================================
//  Solo / mute logic
// ===========================================================================
bool Sequencer::anySolo() const {
  for (uint8_t t = 0; t < MAX_TRACKS; t++) if (data.tracks[t].solo) return true;
  return false;
}
bool Sequencer::trackAudible(uint8_t t) const {
  if (anySolo()) return data.tracks[t].solo != 0;
  return data.tracks[t].mute == 0;
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
    case SF_NOTE:  sp.note  = (uint8_t)clampi(sp.note  + delta, 0, 127); break;
    case SF_VEL:   sp.vel   = (uint8_t)clampi(sp.vel   + delta, 1, 127); break;
    case SF_GATE:  sp.gate  = (uint8_t)clampi(sp.gate  + delta * 5, 1, 100); break;
    case SF_PROB:  sp.prob  = (uint8_t)clampi(sp.prob  + delta * 5, 0, 100); break;
    case SF_MICRO: sp.micro = (int8_t) clampi(sp.micro + delta, -12, 12); break;
    default: break;
  }
}

void Sequencer::setProgram(uint8_t t, int delta){ data.tracks[t].program = (uint8_t)clampi(data.tracks[t].program + delta, 0, 127); }
void Sequencer::setBank(uint8_t t){ data.tracks[t].bankMSB = data.tracks[t].bankMSB ? 0 : 127; }
void Sequencer::setChannel(uint8_t t, int delta){ data.tracks[t].channel = (uint8_t)clampi(data.tracks[t].channel + delta, 1, 16); }
void Sequencer::setOctave(uint8_t t, int delta){ data.tracks[t].octave = (int8_t)clampi(data.tracks[t].octave + delta, -3, 3); }
void Sequencer::setLength(uint8_t t, int delta){ data.tracks[t].length = (uint8_t)clampi(data.tracks[t].length + delta, 1, MAX_STEPS); }
void Sequencer::setVol(uint8_t t, int delta){ data.tracks[t].vol = (uint8_t)clampi(data.tracks[t].vol + delta * 4, 0, 127); }
void Sequencer::setPan(uint8_t t, int delta){ data.tracks[t].pan = (uint8_t)clampi(data.tracks[t].pan + delta * 4, 0, 127); }
void Sequencer::setRevSend(uint8_t t, int delta){ data.tracks[t].revSend = (uint8_t)clampi(data.tracks[t].revSend + delta * 4, 0, 127); }
void Sequencer::setChoSend(uint8_t t, int delta){ data.tracks[t].choSend = (uint8_t)clampi(data.tracks[t].choSend + delta * 4, 0, 127); }
void Sequencer::toggleMute(uint8_t t){ data.tracks[t].mute ^= 1; }
void Sequencer::toggleSolo(uint8_t t){ data.tracks[t].solo ^= 1; }

void Sequencer::setBpm(int delta){ data.bpm = (uint16_t)clampi((int)data.bpm + delta, BPM_MIN, BPM_MAX); }
void Sequencer::setSwing(int delta){ data.swing = (uint8_t)clampi(data.swing + delta, 0, 75); }
void Sequencer::setSpb(int delta){ data.spbIndex = (uint8_t)clampi(data.spbIndex + delta, 0, (int)NUM_SPB_OPTIONS - 1); }
void Sequencer::setRevType(int delta){ data.revType = (uint8_t)clampi(data.revType + delta, 0, 7); }
void Sequencer::setChoType(int delta){ data.choType = (uint8_t)clampi(data.choType + delta, 0, 7); }
void Sequencer::setMasterVol(int delta){ data.masterVol = (uint8_t)clampi(data.masterVol + delta * 4, 0, 127); }

void Sequencer::clearTrack(uint8_t t){
  for (uint8_t s = 0; s < MAX_STEPS; s++) data.tracks[t].steps[s].active = 0;
}

// Replace the whole song from core0. The engine (core1) reads `data` every pass, so a
// multi-KB struct copy must not run concurrently: stop the transport, park core1 with
// the quiesce handshake, swap, then release behind a barrier so the engine observes the
// new data before it resumes. All waits are bounded -> a stalled engine can't hang the UI.
void Sequencer::loadSong(const Song& src) {
  reqStop = 1;                                            // ask the engine to stop
  uint32_t t0 = millis();
  while (isRunning && (millis() - t0) < 80) delay(2);

  reqQuiesce = 1;                                         // ask core1 to park
  t0 = millis();
  while (!quiesced && (millis() - t0) < 50) delay(1);     // wait for the ack

  data = src;                 // safe: core1 is parked (worst case timed out -> as before)
  reqResendAll = 1;           // re-push every setting to the synth once core1 resumes
  __dmb();                    // release: publish the new `data` before clearing the flag
  reqQuiesce = 0;             // release core1
}

// ===========================================================================
//  ENGINE (core1)
// ===========================================================================
void Sequencer::engineBegin() {
  for (int i = 0; i < MAX_EVENTS; i++) events[i].used = 0;
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
    if (chChanged || sh.vol != tr.vol) { GMSynth::setVolume(ch, tr.vol); sh.vol = tr.vol; }
    if (chChanged || sh.pan != tr.pan) { GMSynth::setPan(ch, tr.pan); sh.pan = tr.pan; }
    if (chChanged || sh.rev != tr.revSend) { GMSynth::setReverbSend(ch, tr.revSend); sh.rev = tr.revSend; }
    if (chChanged || sh.cho != tr.choSend) { GMSynth::setChorusSend(ch, tr.choSend); sh.cho = tr.choSend; }
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
  if (full || shMasterVol != data.masterVol) {
    GMSynth::masterVolume(data.masterVol);
    shMasterVol = data.masterVol;
  }
  shadowValid = true;
}

void Sequencer::startFromTop() {
  flushAllOffs();
  for (int i = 0; i < MAX_EVENTS; i++) events[i].used = 0;
  gTick = 0; accFrac = 0;
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
  // queue full: drop silently (extremely unlikely with MAX_EVENTS=160)
}

void Sequencer::triggerStep(uint32_t stepCount) {
  const uint16_t tps = ticksPerStep();
  const int swingTicks = ((int)data.swing * tps) / 100;
  const bool offBeat = (stepCount & 1u);

  for (uint8_t t = 0; t < MAX_TRACKS; t++) {
    Track& tr = data.tracks[t];
    uint8_t len = tr.length; if (len < 1) len = 1;
    uint8_t si = (uint8_t)(stepCount % len);
    tr.curStep = si;                              // playhead for the UI (all tracks)

    if (!trackAudible(t)) continue;
    Step& sp = tr.steps[si];
    if (!sp.active) continue;
    if (sp.prob < 100 && (int)(rng() % 100) >= sp.prob) continue;

    int offset = (offBeat ? swingTicks : 0) + sp.micro;
    if (offset < 0) offset = 0;
    if (offset >= tps) offset = tps - 1;

    uint8_t ch = tr.channel;
    uint8_t note;
    if (ch == DRUM_CHANNEL) {
      note = sp.note;                              // drum map is fixed, no transpose
    } else {
      note = (uint8_t)clampi((int)sp.note + (int)tr.octave * 12, 0, 127);
    }

    int gateTicks = ((int)sp.gate * tps) / 100;
    if (gateTicks < 1) gateTicks = 1;

    uint32_t onTick  = gTick + (uint32_t)offset;
    uint32_t offTick = onTick + (uint32_t)gateTicks;
    scheduleEvent(onTick,  EV_ON,  ch, note, sp.vel);
    scheduleEvent(offTick, EV_OFF, ch, note, 0);
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
  // ---- load-time quiesce: park while core0 swaps the whole Song (see UI::doLoad).
  //      Only engaged for a song load; the playback path stays lock-free. ----
  if (reqQuiesce) {
    quiesced = 1;                                  // tell core0 we have parked
    while (reqQuiesce) tight_loop_contents();      // ...and stop reading `data`
    __dmb();                                        // acquire: see core0's new `data` first
    quiesced = 0;
  }

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
