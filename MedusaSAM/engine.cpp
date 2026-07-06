// ============================================================================
//  Medusa SAM  --  engine.cpp  (runs entirely on core1)
// ============================================================================
#include "engine.h"
#include "sam2695.h"
#include "sound_source.h"

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// A reconcile item costs at most one NRPN (15 bytes) or GS SysEx (11 bytes);
// only send when the UART FIFO has comfortable head-room so the reconcile
// pass can never block the tick loop.
static const int TX_HEADROOM = 24;

// ─── lifecycle ───────────────────────────────────────────────────────────────
void Engine::begin(Song* song) {
    _song = song;
    for (int i = 0; i < MAX_EVENTS; i++) events[i].used = 0;
    for (int t = 0; t < NUM_TRACKS; t++) {
        heldNote[t] = 0xFF; heldChan[t] = 1; heldDest[t] = DEST_SAM;
        tieHeld[t] = 0; trackStep[t] = 0;
    }
    invalidateShadow();
    reqResendAll = 1;              // push the whole song state once after reset
    patIdx  = 0;
    playPat = 0;
    refreshPeriod();
}

void Engine::refreshPeriod() {
    uint16_t bpm = _song->bpm;
    if (bpm < BPM_MIN) bpm = BPM_MIN;
    if (bpm > BPM_MAX) bpm = BPM_MAX;
    uint8_t spb = kStepsPerBeatOptions[_song->spbIndex % NUM_SPB_OPTIONS];
    periodDen   = (uint32_t)bpm * PPQN;
    periodWhole = 60000000UL / periodDen;
    periodFrac  = 60000000UL % periodDen;
    lastBpm = bpm; lastSpb = spb;
}

// ─── mute / solo ─────────────────────────────────────────────────────────────
bool Engine::anySolo() const {
    for (uint8_t t = 0; t < NUM_TRACKS; t++) if (_song->track[t].solo) return true;
    return false;
}
bool Engine::trackAudible(uint8_t t) const {
    if (anySolo()) return _song->track[t].solo != 0;
    return _song->track[t].mute == 0;
}

// ─── settings reconcile (Song -> SAM2695, diffed against a shadow) ──────────
void Engine::invalidateShadow() {
    memset(shadow, 0x80, sizeof(shadow));
    memset(static_cast<void*>(&shFx), 0x80, sizeof(shFx));
}

bool Engine::reconcileTrack(uint8_t t) {
    TrackCfg& c  = _song->track[t];
    TrShadow& sh = shadow[t];
    uint8_t ch = c.channel;
    if (sh.channel != c.channel) {
        // Moving channel: every per-part setting must be re-sent there.  One
        // track's full set fits inside a single TX budget window, so this
        // converges even if interrupted.
        memset(&sh, 0x80, sizeof(sh));
    }

    #define NEED(bytes) do { if (SAM::txFree() < (bytes) + TX_HEADROOM) return false; } while (0)
    #define SYNC(field, bytes, send) \
        if (sh.field != c.field) { NEED(bytes); send; sh.field = c.field; }

    if (sh.bank != c.bankMSB || sh.program != c.program) {
        NEED(5);
        SAM::bankSelect(ch, c.bankMSB);
        SAM::programChange(ch, c.program);       // program must follow bank
        sh.bank = c.bankMSB; sh.program = c.program;
    }
    if (sh.rhythm != c.rhythm) {
        NEED(11);
        // AUTO restores the GS power-on default for that channel.
        uint8_t mode = (c.rhythm == RM_DRUM) ? 1
                     : (c.rhythm == RM_MELODIC) ? 0
                     : (ch == DRUM_CHANNEL ? 1 : 0);
        SAM::setRhythmPart(ch, mode);
        sh.rhythm = c.rhythm;
    }
    SYNC(vol,      3, SAM::setVolume(ch, c.vol));
    SYNC(pan,      3, SAM::setPan(ch, c.pan));
    SYNC(revSend,  3, SAM::setReverbSend(ch, c.revSend));   // NOLINT
    SYNC(choSend,  3, SAM::setChorusSend(ch, c.choSend));
    SYNC(modWheel, 3, SAM::setModWheel(ch, c.modWheel));
    SYNC(cutoff,  15, SAM::setCutoff(ch, c.cutoff));
    SYNC(reso,    15, SAM::setResonance(ch, c.reso));
    SYNC(attack,  15, SAM::setEnvAttack(ch, c.attack));
    SYNC(decay,   15, SAM::setEnvDecay(ch, c.decay));
    SYNC(release, 15, SAM::setEnvRelease(ch, c.release));
    SYNC(vibRate, 15, SAM::setVibratoRate(ch, c.vibRate));
    SYNC(vibDepth,15, SAM::setVibratoDepth(ch, c.vibDepth));
    SYNC(vibDelay,15, SAM::setVibratoDelay(ch, c.vibDelay));
    SYNC(bendRange,15,SAM::setBendRange(ch, c.bendRange));
    if (sh.portaTime != c.portaTime) {
        NEED(6);
        SAM::setPortamento(ch, c.portaTime > 0);
        if (c.portaTime > 0) SAM::setPortamentoTime(ch, c.portaTime);
        sh.portaTime = c.portaTime;
    }
    sh.channel = c.channel;
    #undef SYNC
    #undef NEED
    return true;
}

bool Engine::reconcileFx() {
    FxParams& f = _song->fx;
    #define NEED(bytes) do { if (SAM::txFree() < (bytes) + TX_HEADROOM) return false; } while (0)
    #define SYNCF(field, bytes, send) \
        if (shFx.field != f.field) { NEED(bytes); send; shFx.field = f.field; }

    SYNCF(revType,     3, SAM::setReverbProgram(f.revType));
    SYNCF(revLevel,   11, SAM::setReverbLevel(f.revLevel));
    SYNCF(revTime,    11, SAM::setReverbTime(f.revTime));
    SYNCF(revFeedback,11, SAM::setReverbDelayFeedback(f.revFeedback));
    SYNCF(revPreLpf,  11, SAM::setReverbPreLpf(f.revPreLpf));
    SYNCF(choType,     3, SAM::setChorusProgram(f.choType));
    SYNCF(choLevel,   11, SAM::setChorusLevel(f.choLevel));
    SYNCF(choFeedback,11, SAM::setChorusFeedback(f.choFeedback));
    SYNCF(choDelay,   11, SAM::setChorusDelay(f.choDelay));
    SYNCF(choRate,    11, SAM::setChorusRate(f.choRate));
    SYNCF(choDepth,   11, SAM::setChorusDepth(f.choDepth));
    SYNCF(choToRev,   11, SAM::setChorusToReverb(f.choToRev));
    SYNCF(choPreLpf,  11, SAM::setChorusPreLpf(f.choPreLpf));
    for (uint8_t b = 0; b < 4; b++) {
        if (shFx.eqGain[b] != f.eqGain[b]) {
            NEED(15); SAM::setEqGain(b, f.eqGain[b]); shFx.eqGain[b] = f.eqGain[b];
        }
        if (shFx.eqFreq[b] != f.eqFreq[b]) {
            NEED(15); SAM::setEqFreq(b, f.eqFreq[b]); shFx.eqFreq[b] = f.eqFreq[b];
        }
    }
    SYNCF(masterVol,   8, SAM::masterVolume(f.masterVol));
    SYNCF(keyShift,   11, SAM::setMasterKeyShift(f.keyShift));
    SYNCF(fineTune,   14, SAM::setMasterTuneCents(f.fineTune));
    SYNCF(masterPan,  11, SAM::setMasterPanGS(f.masterPan));
    #undef SYNCF
    #undef NEED
    return true;
}

void Engine::reconcile() {
    if (reqResendAll) { reqResendAll = 0; invalidateShadow(); }
    for (uint8_t t = 0; t < NUM_TRACKS; t++)
        if (!reconcileTrack(t)) return;      // budget spent; resume next pass
    reconcileFx();
}

// ─── transport ───────────────────────────────────────────────────────────────
void Engine::applyPatternSwitch(uint8_t idx) {
    if (idx >= NUM_PATTERNS) return;
    patIdx  = idx;
    playPat = idx;
    stepCount = 0;
    barPos    = 0;
}

void Engine::startFromTop() {
    flushAllOffs();
    for (int i = 0; i < MAX_EVENTS; i++) events[i].used = 0;
    gTick = 0; accFrac = 0; stepCount = 0; barPos = 0; chainPos = 0;
    if (_song->chainLen > 0 && _song->chain[0] < NUM_PATTERNS)
        applyPatternSwitch(_song->chain[0]);
    refreshPeriod();
    eRunning = true; isRunning = 1;
    nextUs = time_us_64();
    if (_song->clockOut) SAM::start();
}

void Engine::stopTransport() {
    eRunning = false; isRunning = 0;
    if (_song->clockOut) SAM::stop();
    flushAllOffs();
}

void Engine::flushAllOffs() {
    for (int i = 0; i < MAX_EVENTS; i++)
        if (events[i].used && events[i].type == EV_ON) events[i].used = 0;
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (events[i].used && events[i].type == EV_OFF) {
            SoundSource::noteOff(events[i].dest, events[i].ch, events[i].note);
            events[i].used = 0;
        }
    }
    for (int t = 0; t < NUM_TRACKS; t++) {
        if (tieHeld[t] && heldNote[t] != 0xFF)
            SoundSource::noteOff(heldDest[t], heldChan[t], heldNote[t]);
        tieHeld[t] = 0; heldNote[t] = 0xFF;
    }
    SoundSource::allOff();       // belt and braces
}

// ─── event scheduler ─────────────────────────────────────────────────────────
void Engine::scheduleEvent(uint32_t tick, uint8_t type, uint8_t dest,
                           uint8_t ch, uint8_t note, uint8_t vel) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (!events[i].used) {
            events[i].tick = tick; events[i].type = type; events[i].dest = dest;
            events[i].ch = ch; events[i].note = note; events[i].vel = vel;
            events[i].used = 1;
            return;
        }
    }
    // pool exhausted: drop (inaudible in practice; pool is sized generously)
}

void Engine::serviceEvents(uint32_t curTick) {
    // OFFs first so a same-tick retrigger isn't killed by a stale note-off
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (events[i].used && events[i].type == EV_OFF &&
            (int32_t)(curTick - events[i].tick) >= 0) {
            SoundSource::noteOff(events[i].dest, events[i].ch, events[i].note);
            events[i].used = 0;
        }
    }
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (events[i].used && events[i].type == EV_ON &&
            (int32_t)(curTick - events[i].tick) >= 0) {
            SoundSource::noteOn(events[i].dest, events[i].ch, events[i].note, events[i].vel);
            events[i].used = 0;
        }
    }
}

void Engine::releaseHeld(uint8_t t, uint32_t atTick) {
    if (tieHeld[t] && heldNote[t] != 0xFF)
        scheduleEvent(atTick, EV_OFF, heldDest[t], heldChan[t], heldNote[t], 0);
    tieHeld[t] = 0;
    heldNote[t] = 0xFF;
}

// ─── step trigger ────────────────────────────────────────────────────────────
void Engine::triggerStep() {
    Pattern& p = _song->pattern[patIdx];
    const uint16_t tps = ticksPerStep();
    const int swingTicks = ((int)p.swing * tps) / 100;
    const bool offBeat = (stepCount & 1u);

    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        TrackCfg& c = _song->track[t];
        uint8_t len = p.tlen[t]; if (len < 1) len = 1; if (len > NUM_STEPS) len = NUM_STEPS;
        uint8_t si = (uint8_t)(stepCount % len);
        trackStep[t] = si;                       // playhead for the UI

        bool drum = trackIsDrum(c);
        Step& sp = p.step[t][si];

        bool fires = trackAudible(t) && stepOn(sp) &&
                     !(sp.prob < 100 && (int)(rng() % 100) >= sp.prob);

        if (!fires) { releaseHeld(t, gTick); continue; }

        int offset = (offBeat ? swingTicks : 0) + sp.micro;
        offset = clampi(offset, 0, (int)tps - 1);
        uint32_t onTick = gTick + (uint32_t)offset;

        uint8_t note;
        if (drum) note = sp.note;
        else {
            note = scaleSnap(_song->scaleRoot, _song->scaleType, sp.note);
            note = (uint8_t)clampi((int)note + (int)c.octave * 12, 0, 127);
        }
        uint8_t vel = sp.vel;
        if (stepAccent(sp)) vel = (uint8_t)clampi((int)vel + 25, 1, 127);

        uint8_t r = sp.ratchet; if (r < 1) r = 1; if (r > 4) r = 4;
        bool tie = stepTie(sp) && !drum && r == 1;   // ratchets override tie

        if (tieHeld[t] && heldNote[t] == note && r == 1) {
            // Same pitch held over: never retrigger.  Either keep holding
            // (tie continues) or end the chain at this step's gate.
            if (!tie) {
                int gateTicks = clampi(((int)sp.gate * tps) / 100, 1, 4 * (int)tps);
                scheduleEvent(onTick + gateTicks, EV_OFF, heldDest[t], heldChan[t], note, 0);
                tieHeld[t] = 0; heldNote[t] = 0xFF;
            }
            continue;
        }

        if (tieHeld[t]) {
            // Legato hand-over: new note ON first, old OFF one tick later so
            // the SAM's portamento/mono behaviour glides instead of gapping.
            scheduleEvent(onTick + 1, EV_OFF, heldDest[t], heldChan[t], heldNote[t], 0);
            tieHeld[t] = 0; heldNote[t] = 0xFF;
        }

        if (r == 1) {
            scheduleEvent(onTick, EV_ON, c.dest, c.channel, note, vel);
            if (tie) {
                tieHeld[t] = 1; heldNote[t] = note;
                heldChan[t] = c.channel; heldDest[t] = c.dest;
            } else {
                int gateTicks = clampi(((int)sp.gate * tps) / 100, 1, 4 * (int)tps);
                scheduleEvent(onTick + gateTicks, EV_OFF, c.dest, c.channel, note, 0);
            }
        } else {
            // Ratchet: r evenly-spaced sub-hits inside the step.
            uint16_t sub = tps / r; if (sub < 2) sub = 2;
            int gateTicks = clampi(((int)sp.gate * sub) / 100, 1, (int)sub - 1);
            for (uint8_t k = 0; k < r; k++) {
                uint32_t hit = onTick + (uint32_t)k * sub;
                scheduleEvent(hit, EV_ON, c.dest, c.channel, note, vel);
                scheduleEvent(hit + gateTicks, EV_OFF, c.dest, c.channel, note, 0);
            }
        }
    }
}

// ─── the 96-PPQN tick ────────────────────────────────────────────────────────
void Engine::tick() {
    if (_song->bpm != lastBpm ||
        kStepsPerBeatOptions[_song->spbIndex % NUM_SPB_OPTIONS] != lastSpb)
        refreshPeriod();

    // next tick time: drift-free integer accumulator
    uint32_t inc = periodWhole;
    accFrac += periodFrac;
    if (accFrac >= periodDen) { accFrac -= periodDen; inc += 1; }
    nextUs += inc;

    if (_song->clockOut && (gTick % MIDI_CLOCK_DIV) == 0) SAM::clockTick();

    const uint16_t tps = ticksPerStep();
    if ((gTick % tps) == 0) {
        // Bar / chain bookkeeping happens ON the step boundary, before firing.
        Pattern& p = _song->pattern[patIdx];
        uint8_t barLen = p.length; if (barLen < 1) barLen = 1;
        if (barPos >= barLen) {
            barPos = 0;
            // 1) queued manual pattern switch wins
            uint8_t rq = reqPattern;
            if (rq != 0xFF) { reqPattern = 0xFF; applyPatternSwitch(rq); chainPos = 0; }
            // 2) otherwise follow the song chain
            else if (_song->chainLen > 1) {
                chainPos = (uint8_t)((chainPos + 1) % _song->chainLen);
                uint8_t nxt = _song->chain[chainPos];
                if (nxt >= NUM_PATTERNS) { chainPos = 0; nxt = _song->chain[0]; }
                if (nxt < NUM_PATTERNS && nxt != patIdx) applyPatternSwitch(nxt);
                else { stepCount = 0; }      // same pattern: restart the bar cleanly
            }
        }
        triggerStep();
        stepCount++;
        barPos++;
    }

    serviceEvents(gTick);
    gTick++;
}

// ─── the core1 service loop ──────────────────────────────────────────────────
void Engine::service() {
    // flash-safe park: acknowledge, then spin until core0 releases us.  The
    // UART is idle for the whole window, so LittleFS writes can't collide
    // with a half-sent MIDI message (core0 additionally locks core1 out of
    // XIP flash via rp2040.idleOtherCore() during the actual write).
    if (reqPause) {
        if (eRunning) stopTransport();
        paused = 1;
        while (reqPause) { tight_loop_contents(); }
        paused = 0;
        reqResendAll = 1;            // a load may have replaced the whole song
        return;
    }

    // one-shot requests
    if (reqPanic)   { reqPanic = 0; flushAllOffs(); }
    if (reqGmReset) { reqGmReset = 0; SAM::gmReset(); delay(20); reqResendAll = 1; }
    if (reqGsReset) { reqGsReset = 0; SAM::gsReset(); delay(20); reqResendAll = 1; }

    // XPRT raw-send mailbox
    if (xKind) {
        uint8_t k = xKind;
        if (k == 1) SAM::cc(xA, xB, xC);
        else if (k == 2) SAM::nrpn(xA, xB, xC, xD);
        else if (k == 3) SAM::gsParam(xA, xB, xC, xD);
        xKind = 0;
    }

    // settings -> synth (budgeted; never blocks the tick)
    reconcile();

    // transport requests
    if (reqStart)    { reqStart = 0;    startFromTop(); }
    if (reqContinue && !eRunning) {
        reqContinue = 0;
        refreshPeriod();
        eRunning = true; isRunning = 1;
        nextUs = time_us_64();
        if (_song->clockOut) SAM::cont();
    } else if (reqContinue) reqContinue = 0;
    if (reqStop)     { reqStop = 0;     stopTransport(); }

    // immediate pattern switch while stopped
    if (!eRunning && reqPattern != 0xFF) {
        uint8_t rq = reqPattern; reqPattern = 0xFF;
        applyPatternSwitch(rq);
    }

    // audition (wall-clock, independent of the transport)
    if (audReq) {
        audReq = 0;
        if (audActive) SAM::noteOff(audOnCh, audOnNote);
        audOnCh = audCh; audOnNote = audNote;
        SAM::noteOn(audOnCh, audOnNote, audVel);
        audActive = true;
        audOffUs = time_us_64() + 300000ULL;
    }
    if (audActive && time_us_64() >= audOffUs) {
        SAM::noteOff(audOnCh, audOnNote);
        audActive = false;
    }

    // timing
    if (eRunning) {
        uint64_t now = time_us_64();
        if (now >= nextUs) {
            if (now - nextUs > 50000ULL) nextUs = now;   // stall resync guard
            tick();
        } else {
            uint64_t remaining = nextUs - now;
            if (remaining > 200) sleep_us((uint32_t)(remaining - 100));
            else { while (time_us_64() < nextUs) tight_loop_contents(); tick(); }
        }
    } else {
        sleep_us(200);
    }
}
